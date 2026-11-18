#!/usr/bin/env bash
# Build the Linux suite installer (.run) from a stage: setup.sh, plugins.list, suite, glibc, stage/.
# Usage: installer/linux/build.sh <stage-dir> <out-dir>   (tools/installer.sh linux calls it)
set -euo pipefail

. "$(dirname "$0")/../common.sh"

stage="${1:?usage: installer/linux/build.sh <stage-dir> <out-dir>}"
out="${2:?usage: installer/linux/build.sh <stage-dir> <out-dir>}"
here="$MYZEL_ROOT/installer/linux"
makeself_version="2.7.1"
makeself_sha256="96843e5db70da71dcb3cb594a6899886043f115e9205c6b5ca2f26000389514e"

suite="$(myzel_suite_version)"
[ -n "$suite" ] || { echo "installer/VERSION is empty." >&2; exit 1; }
run="$out/MyzelAudio-$suite-linux-x86_64.run"

find_makeself()
{
    local bin_dir="$MYZEL_ROOT/tools/.bin" dir="$MYZEL_ROOT/tools/.bin/makeself-$makeself_version" tmp
    if command -v makeself > /dev/null 2>&1; then
        command -v makeself
        return 0
    fi
    if command -v makeself.sh > /dev/null 2>&1; then
        command -v makeself.sh
        return 0
    fi
    if [ ! -f "$dir/makeself.sh" ]; then
        echo "Downloading makeself $makeself_version..." >&2
        mkdir -p "$bin_dir"
        tmp="$(mktemp -d)"
        curl -fsSL -o "$tmp/makeself.tar.gz" \
            "https://github.com/megastep/makeself/archive/refs/tags/release-$makeself_version.tar.gz"
        if [ "$(sha256sum "$tmp/makeself.tar.gz" | cut -d' ' -f1)" != "$makeself_sha256" ]; then
            echo "makeself $makeself_version download has an unexpected SHA-256; refusing to use it." >&2
            rm -rf "$tmp"
            exit 1
        fi
        rm -rf "$dir"
        mkdir -p "$dir"
        tar -xzf "$tmp/makeself.tar.gz" -C "$dir" --strip-components=1
        rm -rf "$tmp"
    fi
    echo "$dir/makeself.sh"
}

# The highest GLIBC_x.y symbol version any staged binary needs.
required_glibc()
{
    local f
    find "$1" -type f | while IFS= read -r f; do
        [ "$(head -c 4 "$f" | od -An -c | tr -d ' ')" = '177ELF' ] || continue
        if command -v objdump > /dev/null 2>&1; then
            objdump -T "$f" 2> /dev/null || true
        else
            readelf --dyn-syms -W "$f" 2> /dev/null || true
        fi
    done | grep -oE 'GLIBC_[0-9]+\.[0-9]+(\.[0-9]+)?' | sed 's/^GLIBC_//' | sort -uV | tail -n 1
}

work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT
payload="$work/payload"
mkdir -p "$payload/stage"

cp "$here/setup.sh" "$payload/setup.sh"
chmod 755 "$payload/setup.sh"
printf '%s\n' "$suite" > "$payload/suite"
: > "$payload/plugins.list"

for id in $(myzel_plugin_ids); do
    name="$(myzel_plugin_name "$id")"
    blurb="$(myzel_plugin_blurb "$id")"
    version="$(myzel_stage_version "$stage" "$id")"
    formats=""
    for format in $(myzel_stage_formats "$stage" "$id"); do
        case "$format" in
            VST3 | CLAP | LV2 | Standalone) formats="${formats:+$formats,}$format" ;;
            *) echo "Ignoring $id/$format: not a Linux format." >&2 ;;
        esac
    done
    if [ -z "$name" ] || [ -z "$version" ] || [ -z "$formats" ]; then
        echo "$id: needs a name in plugins.tsv, a version and at least one Linux format in $stage/$id." >&2
        exit 1
    fi
    printf '%s\t%s\t%s\t%s\t%s\n' "$id" "$name" "$version" "$formats" "$blurb" >> "$payload/plugins.list"
    mkdir -p "$payload/stage/$id"
    for format in ${formats//,/ }; do
        cp -R "$stage/$id/$format" "$payload/stage/$id/$format"
    done
    if [ -f "$stage/$id/icon.svg" ]; then cp "$stage/$id/icon.svg" "$payload/stage/$id/icon.svg"; fi
    echo "  $name $version: ${formats//,/, }"
done

glibc="$(required_glibc "$payload/stage")"
[ -n "$glibc" ] || { echo "Could not read the glibc version the staged binaries need (objdump or readelf)." >&2; exit 1; }
printf '%s\n' "$glibc" > "$payload/glibc"
echo "  needs glibc $glibc or newer"

makeself="$(find_makeself)"
mkdir -p "$out"
rm -f "$run"
# xz: the four formats of a plugin share most of their code, which a large xz window finds (gzip: ~5x bigger).
bash "$makeself" --nox11 --xz --sha256 --tar-quietly \
    "$payload" "$run" "Myzel Audio $suite" bash ./setup.sh
echo "$run"
