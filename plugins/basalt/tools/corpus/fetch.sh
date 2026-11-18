#!/usr/bin/env bash
# Fetch Basalt's test corpus into the local cache for the [.corpus] tier (PLAN.md, Measurement and
# quality gates). Never writes into the repository.
#
# Usage: plugins/basalt/tools/corpus/fetch.sh [options] [target ...]
#   targets:  albumdb-mixes  AlbumDB's ten stereo masters (indie rock and folk, 44.1 kHz)
#             albumdb-stems  AlbumDB's raw stems: kick, snare, overheads, bass, lead vocal, piano,
#                            acoustic and electric guitar, where a song has them
#             guitar-techs   Guitar-TECHS' twelve musical excerpts, DI and miked amp (48 kHz)
#             guitarset      GuitarSet's 180 strummed accompaniments, mono microphone (44.1 kHz)
#             vctk           VCTK 0.92, every sentence of four speakers, mic2 (48 kHz)
#             all            all of the above (the default)
#             candidates     what make_excerpts.py's selection rule reads (choose)
#             excerpts       only the sources of the committed excerpts (make_excerpts.py build)
#   --dry-run                say what would be fetched and how big it is; write nothing
#   --jobs N                 connections per archive (default 4; Zenodo serves each one slowly)
#
# The cache is $BASALT_CORPUS_DIR, default ~/.cache/myzel/basalt-corpus. Each finished full-tier
# target leaves a stamp there, so a second run skips it; members already extracted at full size are
# skipped, so an interrupted run resumes. Each archive's MD5 is checked with its host first
# (sources.py check), each member's CRC-32 while it is read (remotezip.py).
set -euo pipefail

here="$(cd "$(dirname "$0")" && pwd)"
cache="${BASALT_CORPUS_DIR:-$HOME/.cache/myzel/basalt-corpus}"
dry_run=0
jobs=4
targets=()

while [[ $# -gt 0 ]]; do
    case "$1" in
        --dry-run) dry_run=1 ;;
        --jobs) jobs="${2:?--jobs needs a value}"; shift ;;
        -h|--help) sed -n '2,/^set -euo/{/^set -euo/d;s/^# \{0,1\}//;p}' "$0"; exit 0 ;;
        all) mapfile -t -O "${#targets[@]}" targets < <(python3 -I "$here/sources.py" full-tier) ;;
        *)
            if python3 -I "$here/sources.py" targets | grep -qx -- "$1"; then
                targets+=("$1")
            else
                echo "Unknown argument '$1' (see --help)." >&2; exit 2
            fi ;;
    esac
    shift
done
[[ ${#targets[@]} -gt 0 ]] || mapfile -t targets < <(python3 -I "$here/sources.py" full-tier)

repo="$(cd "$here/../../../.." && pwd)"
case "$(realpath -m "$cache")/" in
    "$repo"/*) echo "The cache ($cache) must be outside the repository." >&2; exit 2 ;;
esac

say() { printf '%s\n' "$*"; }
human() { numfmt --to=si --round=nearest --suffix=B --format=%.1f "$1"; }
stamp() { [[ -f "$cache/.stamps/$1" ]]; }
mark() { mkdir -p "$cache/.stamps"; date -Is > "$cache/.stamps/$1"; }

free_bytes() {
    local d="$cache"
    while [[ ! -d "$d" ]]; do d="$(dirname "$d")"; done
    df --output=avail -B1 "$d" | tail -1
}

need_space() {
    local bytes="$1" margin=$((1 << 30)) avail
    avail="$(free_bytes)"
    if (( avail < bytes + margin )); then
        say "  not enough space: needs $(human "$bytes") plus 1 GiB margin, $(human "$avail") free" >&2
        [[ $dry_run -eq 1 ]] || exit 1
    fi
}

declare -A checked=()
TOTAL_MISSING=0

# archive key, url, destination, globs...
fetch_members() {
    local key="$1" url="$2" dest="$3" sizes members bytes transfer missing
    shift 3
    if [[ -z "${checked[$key]:-}" ]]; then
        python3 -I "$here/sources.py" check "$key"
        checked[$key]=1
    fi
    sizes="$(python3 -I "$here/remotezip.py" size "$url" --dest "$dest" "$@")"
    members="$(awk '/^members/ { print $2 }' <<< "$sizes")"
    bytes="$(awk '/^bytes/ { print $2 }' <<< "$sizes")"
    transfer="$(awk '/^transfer/ { print $2 }' <<< "$sizes")"
    missing="$(awk '/^missing/ { print $2 }' <<< "$sizes")"
    say "  $key: $members members, $(human "$bytes") ($(human "$transfer") compressed), $(human "$missing") not yet here"
    if (( members == 0 )); then
        say "  no member matches; the selection names files the archive does not hold" >&2
        exit 1
    fi
    TOTAL_MISSING=$((TOTAL_MISSING + missing))
    need_space "$missing"
    [[ $dry_run -eq 1 ]] && return
    mkdir -p "$dest"
    python3 -I "$here/remotezip.py" extract --jobs "$jobs" "$url" "$dest" "$@"
}

for tool in curl python3 ffprobe numfmt; do
    command -v "$tool" > /dev/null || { echo "Needs $tool." >&2; exit 1; }
done

say "Cache: $cache ($(human "$(free_bytes)") free)$([[ $dry_run -eq 1 ]] && echo ', dry run')"
for target in "${targets[@]}"; do
    say "$target"
    # candidates and excerpts follow make_excerpts.py and excerpts.tsv, so they are never stamped.
    frozen=1
    [[ "$target" != candidates && "$target" != excerpts ]] || frozen=0
    if [[ $frozen -eq 1 ]] && stamp "$target"; then
        say "  done"
        continue
    fi
    if [[ "$target" == excerpts && ! -f "$here/../../tests/corpus/excerpts.tsv" ]]; then
        say "  no excerpts.tsv yet: fetch candidates and run make_excerpts.py choose first" >&2
        exit 1
    fi
    mapfile -t lines < <(python3 -I "$here/sources.py" plan "$target" "$cache")
    [[ ${#lines[@]} -gt 0 ]] || { say "  sources.py could not plan $target" >&2; exit 1; }
    for line in "${lines[@]}"; do
        IFS=$'\t' read -r -a fields <<< "$line"
        fetch_members "${fields[@]}"
    done
    if [[ $dry_run -eq 0 ]]; then
        [[ "$target" != excerpts ]] || python3 -I "$here/make_excerpts.py" verify --cache "$cache"
        [[ $frozen -eq 0 ]] || mark "$target"
    fi
done

if [[ $dry_run -eq 1 ]]; then
    say "Would fetch $(human "$TOTAL_MISSING") (uncompressed size on disk)."
else
    python3 -I "$here/sources.py" manifest "$cache"
fi
