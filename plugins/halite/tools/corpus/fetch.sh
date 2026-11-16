#!/usr/bin/env bash
# Fetch Halite's full test corpora into the local cache, verify them and build phone labels for
# the [.corpus] tier (PLAN.md, Measurement and quality gates). Never writes into the repository.
#
# Usage: plugins/halite/tools/corpus/fetch.sh [options] [target ...]
#   targets:  librispeech  LibriSpeech test-clean (+ --librispeech-subsets), MFA alignments
#             vctk         VCTK 0.92, the MKH 800 channel (mic2), aligned here
#             vocalset     VocalSet 1.0 with the corrected Annotated-VocalSet notes (Donaher's
#                          deposit), vowel labels
#             excerpts     only the sources of the committed excerpts (make_excerpts.py),
#                          always aligned with pocketsphinx
#             all          librispeech vctk vocalset (the default)
#   --dry-run              say what would be fetched and how big it is; write nothing
#   --full-archive         download whole zips and check their published MD5 (VCTK 11.7 GB,
#                          VocalSet 2.5 GB) instead of reading only the needed members
#   --keep-archives        keep downloaded archives after extraction
#   --aligner A            auto (default: mfa when on PATH, else pocketsphinx), mfa, pocketsphinx;
#                          for vctk (the excerpts always use pocketsphinx)
#   --librispeech-subsets  comma list of test-clean, test-other, dev-clean, dev-other
#   --jobs N               aligner processes (default: all cores)
#   --subset NAME          only what subset NAME chooses by rule from vctk and vocalset (m1:
#                          40 VCTK speakers x 40 recordings, VocalSet sung /i/ and /e/); writes
#                          labels/NAME/<corpus>.tsv with the tune/holdout split for each target
#                          (subset.py, README.md)
#
# The cache is $HALITE_CORPUS_DIR, default ~/.cache/myzel/halite-corpus. Each finished step leaves
# a stamp there, so a second run skips it; an interrupted download or member fetch resumes.
set -euo pipefail

here="$(cd "$(dirname "$0")" && pwd)"
cache="${HALITE_CORPUS_DIR:-$HOME/.cache/myzel/halite-corpus}"
dry_run=0
full_archive=0
keep_archives=0
aligner=auto
subsets=test-clean
jobs="$(nproc)"
subset_name=""
targets=()

while [[ $# -gt 0 ]]; do
    case "$1" in
        --dry-run) dry_run=1 ;;
        --full-archive) full_archive=1 ;;
        --keep-archives) keep_archives=1 ;;
        --aligner) aligner="${2:?--aligner needs a value}"; shift ;;
        --librispeech-subsets) subsets="${2:?--librispeech-subsets needs a value}"; shift ;;
        --jobs) jobs="${2:?--jobs needs a value}"; shift ;;
        --subset) subset_name="${2:?--subset needs a name}"; shift ;;
        -h|--help) sed -n '2,/^set -euo/{/^set -euo/d;s/^# \{0,1\}//;p}' "$0"; exit 0 ;;
        librispeech|vctk|vocalset|excerpts) targets+=("$1") ;;
        all) targets+=(librispeech vctk vocalset) ;;
        *) echo "Unknown argument '$1' (see --help)." >&2; exit 2 ;;
    esac
    shift
done
[[ ${#targets[@]} -gt 0 ]] || targets=(librispeech vctk vocalset)

if [[ -n "$subset_name" ]] && ! python3 -I "$here/subset.py" names | grep -qx -- "$subset_name"; then
    echo "Unknown subset '$subset_name'; known: $(python3 -I "$here/subset.py" names | paste -sd ' ')." >&2
    exit 2
fi

repo="$(cd "$here/../../../.." && pwd)"
case "$(realpath -m "$cache")/" in
    "$repo"/*) echo "The cache ($cache) must be outside the repository." >&2; exit 2 ;;
esac

LIBRISPEECH_URL=https://www.openslr.org/resources/12
declare -A LIBRISPEECH_MD5=(   # https://www.openslr.org/resources/12/md5sum.txt
    [test-clean]=32fa31d27d2e1cad72775fee3f4849a9 [test-other]=fb5a50374b501bb3bac4815ee91d3135
    [dev-clean]=42e2234ba48799c1f50f24a7926300a1 [dev-other]=c8d0bcc9cca99d4f8b62fcc847357931)
ALIGNMENTS_URL=https://zenodo.org/api/records/2619474/files/librispeech_alignments.zip/content
VCTK_URL=https://datashare.ed.ac.uk/server/api/core/bitstreams/535f4286-e54c-4038-838c-a02285e32cb2/content
VCTK_MD5=8a6ba2946b36fcbef0212cad601f4bfa   # DataShare's bitstream record, checkSum
VCTK_MEMBERS=(speaker-info.txt update.txt 'txt/*' 'wav48_silence_trimmed/*_mic2.flac'
              'wav48_silence_trimmed/p280/*_mic1.flac')   # p280 has no mic2 recordings
VOCALSET_URL=https://zenodo.org/api/records/10200775/files/VocalSet.zip/content
VOCALSET_MD5=8d39344bbc775aa040840783ae73cfa4   # the Zenodo record's file checksum
VOCALSET_MEMBERS=(VocalSet/readme-anon.txt 'VocalSet/FULL/*.wav'
                  'VocalSet/annotations/extended 1/with file header/*.csv')

say() { printf '%s\n' "$*"; }
human() { numfmt --to=si --round=nearest --suffix=B --format=%.1f "$1"; }
stamp() { [[ -f "$cache/.stamps/$1" ]]; }
mark() { mkdir -p "$cache/.stamps"; date -Is > "$cache/.stamps/$1"; }

# Bytes behind a URL from a HEAD request; fails when the host can't be reached or names no size.
remote_size() {
    local headers n
    headers="$(curl -sfIL "$1")" || return 1
    n="$(tr -d '\r' <<< "$headers" | awk 'tolower($1) == "content-length:" { n = $2 } END { print n + 0 }')"
    (( n > 0 )) || return 1
    echo "$n"
}

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

# url, md5, file under downloads/
download() {
    local url="$1" md5="$2" file="$cache/downloads/$3" size have
    size="$(remote_size "$url")" || { say "  cannot get the size of $url (unreachable, moved, or no Content-Length)" >&2; exit 1; }
    say "  $3: $(human "$size") from $url"
    if [[ -f "$file" ]]; then say "  already downloaded and checked"; return; fi
    have="$(stat -c %s "$file.part" 2>/dev/null || echo 0)"
    need_space "$((size - have))"
    [[ $dry_run -eq 1 ]] && return
    mkdir -p "$cache/downloads"
    if (( have < size )); then
        curl -fL -# --retry 5 --retry-delay 10 -C - -o "$file.part" "$url"
    fi
    say "  checking MD5"
    if [[ "$(md5sum "$file.part" | cut -d' ' -f1)" != "$md5" ]]; then
        rm -f "$file.part"
        say "  MD5 mismatch for $3; the partial file was removed, run again" >&2
        exit 1
    fi
    mv "$file.part" "$file"
}

# archive under downloads/, destination, members...
unpack() {
    local zip="$cache/downloads/$1" dest="$2" bytes
    shift 2
    bytes="$(python3 -I "$here/remotezip.py" size "$zip" "$@" | awk '/^bytes/ { print $2 }')"
    say "  extracting $(human "$bytes")"
    need_space "$bytes"
    mkdir -p "$dest"
    unzip -q -o "$zip" "$@" -d "$dest"
}

finish_archive() {
    [[ $keep_archives -eq 1 ]] || rm -f "$cache/downloads/$1"
}

# label, url, destination, members...; leaves the bytes that were still missing in REMOTE_MISSING.
remote_members() {
    local label="$1" url="$2" dest="$3" sizes members bytes transfer missing
    shift 3
    sizes="$(python3 -I "$here/remotezip.py" size "$url" --dest "$dest" "$@")"
    members="$(awk '/^members/ { print $2 }' <<< "$sizes")"
    bytes="$(awk '/^bytes/ { print $2 }' <<< "$sizes")"
    transfer="$(awk '/^transfer/ { print $2 }' <<< "$sizes")"
    missing="$(awk '/^missing/ { print $2 }' <<< "$sizes")"
    say "  $label: $members members, $(human "$bytes") ($(human "$transfer") compressed), $(human "$missing") not yet here, from $url"
    need_space "$missing"
    REMOTE_MISSING="$missing"
    [[ $dry_run -eq 1 ]] && return
    mkdir -p "$dest"
    python3 -I "$here/remotezip.py" extract "$url" "$dest" "$@"
}

# corpus, zip (URL or local archive): sets SUBSET_MEMBERS to what subset.py chooses and keeps the
# list in subsets/<name>/ for the manifest. VCTK's choice reads the transcripts through CMUdict.
subset_members() {
    local list="$cache/subsets/$subset_name/$1.members" output py=python3
    if [[ "$1" == vctk ]]; then
        py="$(pocketsphinx_python)"
        output="$("$py" -I "$here/subset.py" members "$subset_name" vctk "$2" "$cache")"
    else
        output="$("$py" -I "$here/subset.py" members "$subset_name" "$1" "$2")"
    fi
    mapfile -t SUBSET_MEMBERS < <(grep -v '^#' <<< "$output")
    say "  subset $subset_name: ${#SUBSET_MEMBERS[@]} members, chosen by rule (subset.py)"
    if [[ $dry_run -eq 0 ]]; then
        mkdir -p "$(dirname "$list")"
        printf '%s\n' "$output" > "$list.part"
        mv "$list.part" "$list"
    fi
}

subset_manifest() {
    [[ -n "$subset_name" && $dry_run -eq 0 ]] || return 0
    python3 -I "$here/subset.py" manifest "$subset_name" "$1" "$cache"
}

pocketsphinx_python() {
    local venv="$cache/venv"
    if [[ ! -x "$venv/bin/python" ]] || ! "$venv/bin/python" -I -c 'import pocketsphinx' 2>/dev/null; then
        say "  setting up pocketsphinx (BSD licence) in $venv" >&2
        python3 -m venv "$venv" >&2
        "$venv/bin/pip" install --quiet --prefer-binary 'pocketsphinx==5.0.4' >&2
    fi
    echo "$venv/bin/python"
}

resolve_aligner() {
    case "$aligner" in
        auto) if command -v mfa > /dev/null; then echo mfa; else echo pocketsphinx; fi ;;
        mfa|pocketsphinx) echo "$aligner" ;;
        *) echo "Unknown aligner '$aligner'." >&2; exit 2 ;;
    esac
}

# name, list file (id, audio, transcript), label dir, [aligner: overrides --aligner]. Records the
# aligner and its version in <label dir>/ALIGNER, which make_excerpts.py checks. pocketsphinx
# skips utterances already labelled, so labels from another aligner or version go first.
align() {
    local name="$1" list="$2" out="$3" which record py=""
    which="${4:-$(resolve_aligner)}"
    if [[ "$which" == mfa ]]; then
        record="mfa $(mfa version) (english_us_arpa)"
    else
        py="$(pocketsphinx_python)"
        record="pocketsphinx $("$py" -I -c 'import importlib.metadata as m; print(m.version("pocketsphinx"))')"
    fi
    mkdir -p "$out"
    if [[ "$(cat "$out/ALIGNER" 2>/dev/null)" != "$record" ]] && compgen -G "$out/*.lab" > /dev/null; then
        say "  $out holds labels from '$(cat "$out/ALIGNER" 2>/dev/null || echo 'an unrecorded aligner')': replacing them"
        find "$out" -maxdepth 1 -name '*.lab' -delete
    fi
    rm -f "$out/ALIGNER"
    if [[ "$which" == mfa ]]; then
        local work="$cache/mfa/$name"
        say "  aligning with the Montreal Forced Aligner (english_us_arpa) in $work"
        rm -rf "$work/corpus"
        mkdir -p "$work/corpus"
        local id audio text speaker
        while IFS=$'\t' read -r id audio text; do
            speaker="$(basename "$(dirname "$audio")")"   # MFA adapts per speaker folder
            mkdir -p "$work/corpus/$speaker"
            ln -sf "$audio" "$work/corpus/$speaker/$id.${audio##*.}"
            printf '%s\n' "$text" > "$work/corpus/$speaker/$id.lab"
        done < "$list"
        mfa model download acoustic english_us_arpa
        mfa model download dictionary english_us_arpa
        mfa align --clean -j "$jobs" "$work/corpus" english_us_arpa english_us_arpa "$work/textgrids"
        python3 -I "$here/labels.py" textgrids "$work/textgrids" "$out"
    else
        say "  aligning with pocketsphinx (CMU en-us model, CMUdict) on $jobs processes"
        "$py" -I "$here/align_pocketsphinx.py" align "$list" "$out" --jobs "$jobs"
    fi
    echo "$record" > "$out/ALIGNER"
}

librispeech() {
    say "LibriSpeech (CC BY 4.0, 16 kHz): ${subsets//,/ }"
    local subset
    for subset in ${subsets//,/ }; do
        [[ -n "${LIBRISPEECH_MD5[$subset]:-}" ]] || { say "  unknown subset '$subset'" >&2; exit 2; }
        if ! stamp "librispeech-$subset"; then
            download "$LIBRISPEECH_URL/$subset.tar.gz" "${LIBRISPEECH_MD5[$subset]}" "$subset.tar.gz"
            if [[ $dry_run -eq 0 ]]; then
                say "  extracting"
                mkdir -p "$cache/librispeech"
                tar -xzf "$cache/downloads/$subset.tar.gz" -C "$cache/librispeech"
                mark "librispeech-$subset"
                finish_archive "$subset.tar.gz"
            fi
        else
            say "  $subset: done"
        fi
        if ! stamp "librispeech-$subset-alignments"; then
            say "  MFA alignments (Lugosch 2019, CC BY 4.0, zenodo.org/records/2619474), per-member CRC"
            remote_members "alignments" "$ALIGNMENTS_URL" "$cache/librispeech/alignments" "$subset/*"
            if [[ $dry_run -eq 0 ]]; then
                python3 -I "$here/labels.py" textgrids "$cache/librispeech/alignments/$subset" \
                    "$cache/labels/librispeech"
                python3 -I "$here/manifest.py" manifest librispeech "$cache"
                mark "librispeech-$subset-alignments"
            fi
        else
            say "  $subset alignments: done"
        fi
    done
    subset_manifest librispeech
}

vctk() {
    say "VCTK 0.92 (CC BY 4.0, 48 kHz), MKH 800 channel (mic2; mic1 for p280)"
    local dest="$cache/vctk" tag=vctk members=("${VCTK_MEMBERS[@]}")
    [[ -z "$subset_name" ]] || tag="vctk-$subset_name"
    if ! stamp "$tag"; then
        if [[ $full_archive -eq 1 ]]; then
            download "$VCTK_URL" "$VCTK_MD5" VCTK-Corpus-0.92.zip
            if [[ $dry_run -eq 0 ]]; then
                if [[ -n "$subset_name" ]]; then
                    unpack VCTK-Corpus-0.92.zip "$dest" speaker-info.txt 'txt/*'
                    subset_members vctk "$cache/downloads/VCTK-Corpus-0.92.zip"
                    members=(speaker-info.txt "${SUBSET_MEMBERS[@]}")
                fi
                unpack VCTK-Corpus-0.92.zip "$dest" "${members[@]}"
                finish_archive VCTK-Corpus-0.92.zip
            fi
        elif [[ -n "$subset_name" ]]; then
            remote_members "VCTK transcripts" "$VCTK_URL" "$dest" speaker-info.txt 'txt/*'
            if [[ $dry_run -eq 0 ]] || { [[ $REMOTE_MISSING -eq 0 ]] && [[ -x "$cache/venv/bin/python" ]]; }; then
                subset_members vctk "$VCTK_URL"
                remote_members "VCTK recordings" "$VCTK_URL" "$dest" "${SUBSET_MEMBERS[@]}"
            else
                say "  then: the recordings subset $subset_name chooses from the transcripts (subset.py)"
            fi
        else
            remote_members "VCTK-Corpus-0.92.zip" "$VCTK_URL" "$dest" "${members[@]}"
        fi
        [[ $dry_run -eq 1 ]] || mark "$tag"
    else
        say "  audio: done"
    fi
    if ! stamp "$tag-labels"; then
        say "  phone alignment: $(resolve_aligner)"
        if [[ $dry_run -eq 0 ]]; then
            if [[ -n "$subset_name" ]]; then
                python3 -I "$here/subset.py" align-list "$subset_name" vctk "$cache"
            else
                python3 -I "$here/manifest.py" align-list vctk "$cache"
            fi
            align vctk "$cache/labels/vctk.align.tsv" "$cache/labels/vctk"
            # labels/vctk.tsv is the full tier's manifest; a subset writes labels/<name>/ instead.
            [[ -n "$subset_name" ]] || python3 -I "$here/manifest.py" manifest vctk "$cache"
            mark "$tag-labels"
        fi
    else
        say "  labels: done"
    fi
    subset_manifest vctk
}

vocalset() {
    say "VocalSet 1.0 + corrected Annotated-VocalSet (Donaher 2023, CC BY 4.0, 44.1 kHz), zenodo.org/records/10200775"
    local dest="$cache/vocalset" tag=vocalset members=("${VOCALSET_MEMBERS[@]}")
    [[ -z "$subset_name" ]] || tag="vocalset-$subset_name"
    if ! stamp "$tag"; then
        if [[ $full_archive -eq 1 ]]; then
            download "$VOCALSET_URL" "$VOCALSET_MD5" VocalSet.zip
            if [[ $dry_run -eq 0 ]]; then
                if [[ -n "$subset_name" ]]; then
                    subset_members vocalset "$cache/downloads/VocalSet.zip"
                    members=("${SUBSET_MEMBERS[@]}")
                fi
                unpack VocalSet.zip "$dest" "${members[@]}"
                finish_archive VocalSet.zip
            fi
        else
            if [[ -n "$subset_name" ]]; then
                subset_members vocalset "$VOCALSET_URL"
                members=("${SUBSET_MEMBERS[@]}")
            fi
            remote_members "VocalSet.zip" "$VOCALSET_URL" "$dest" "${members[@]}"
        fi
        if [[ $dry_run -eq 0 ]]; then
            python3 -I "$here/labels.py" vocalset "$dest/VocalSet/annotations/extended 1/with file header" \
                "$dest/VocalSet/FULL" "$cache/labels/vocalset"
            [[ -n "$subset_name" ]] || python3 -I "$here/manifest.py" manifest vocalset "$cache"
            mark "$tag"
        fi
    else
        say "  done"
    fi
    subset_manifest vocalset
}

excerpts() {
    say "Committed-excerpt sources (VCTK candidates, two VocalSet files): a few MB of members"
    local vocalset_members=() member
    while IFS= read -r member; do vocalset_members+=("$member"); done \
        < <(python3 -I "$here/make_excerpts.py" --print-members)
    if [[ $dry_run -eq 0 ]] && ! stamp excerpts-texts; then
        remote_members "VCTK texts" "$VCTK_URL" "$cache/excerpts/vctk" speaker-info.txt 'txt/*'
        mark excerpts-texts
    fi
    if [[ $dry_run -eq 1 ]]; then
        say "  then: VCTK texts (1.8 MB) and 80 candidate recordings chosen from them (about 11 MB)"
        remote_members "VocalSet" "$VOCALSET_URL" "$cache/excerpts/vocalset" "${vocalset_members[@]}"
        return
    fi
    "$(pocketsphinx_python)" -I "$here/make_excerpts.py" --candidates "$cache" > "$cache/excerpts/candidates.tsv"
    local audio=()
    while IFS=$'\t' read -r _ member _; do audio+=("$member"); done < "$cache/excerpts/candidates.tsv"
    remote_members "VCTK candidates" "$VCTK_URL" "$cache/excerpts/vctk" "${audio[@]}"
    remote_members "VocalSet" "$VOCALSET_URL" "$cache/excerpts/vocalset" "${vocalset_members[@]}"
    python3 -I "$here/make_excerpts.py" --align-list "$cache" > "$cache/excerpts/align.tsv"
    # Pinned: the committed tier's ATTRIBUTION.md describes pocketsphinx labels.
    align excerpts "$cache/excerpts/align.tsv" "$cache/excerpts/labels" pocketsphinx
    say "  sources ready; build the committed tier with make_excerpts.py --out plugins/halite/tests/corpus"
}

for tool in curl python3 md5sum numfmt; do
    command -v "$tool" > /dev/null || { echo "Needs $tool." >&2; exit 1; }
done
for target in "${targets[@]}"; do
    if [[ "$target" == vctk || "$target" == excerpts ]] && ! command -v ffmpeg > /dev/null; then
        echo "Needs ffmpeg." >&2; exit 1
    fi
    if [[ $full_archive -eq 1 && "$target" != librispeech ]] && ! command -v unzip > /dev/null; then
        echo "Needs unzip." >&2; exit 1
    fi
done

say "Cache: $cache ($(human "$(free_bytes)") free)$([[ $dry_run -eq 1 ]] && echo ', dry run')"
for target in "${targets[@]}"; do
    "$target"
done
