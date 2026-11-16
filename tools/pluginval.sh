#!/usr/bin/env bash
# Validate a built plugin with Tracktion's pluginval (VST3 by default, LV2 on request).
# Usage: tools/pluginval.sh <plugin> [build-dir] [strictness 1-10] [formats]
#   plugin:  cerulean, umber, ... (a directory under plugins/)
#   formats: comma list of vst3, lv2 (default vst3). pluginval loads neither CLAP nor a Standalone.
set -euo pipefail

root="$(cd "$(dirname "$0")/.." && pwd)"
plugin="${1:?usage: tools/pluginval.sh <plugin> [build-dir] [strictness] [formats]}"
build_dir="${2:-$root/build}"
strictness="${3:-10}"
formats="${4:-vst3}"
version="v1.0.4"
bin_dir="$root/tools/.bin"
pluginval="$bin_dir/pluginval"

if [[ ! -d "$root/plugins/$plugin" ]]; then
    echo "Unknown plugin '$plugin' (expected a directory under plugins/)." >&2
    exit 1
fi
name="${plugin^}"

if [[ ! -x "$pluginval" ]]; then
    echo "Downloading pluginval $version..."
    mkdir -p "$bin_dir"
    tmp="$(mktemp -d)"
    trap 'rm -rf "$tmp"' EXIT
    curl -fsSL -o "$tmp/pluginval.zip" \
        "https://github.com/Tracktion/pluginval/releases/download/$version/pluginval_Linux.zip"
    unzip -q -o "$tmp/pluginval.zip" -d "$bin_dir"
    chmod +x "$pluginval"
fi

for format in ${formats//,/ }; do
    case "$format" in
        vst3) bundle="$(find "$build_dir" -type d -name "$name.vst3" -path '*VST3*' | head -n 1)" ;;
        lv2)  bundle="$(find "$build_dir" -type d -name "$name.lv2" -path '*LV2*' | head -n 1)" ;;
        *) echo "Unknown format '$format' (expected vst3 or lv2)." >&2; exit 1 ;;
    esac
    if [[ -z "$bundle" ]]; then
        echo "$name.$format not found under $build_dir. Build first." >&2
        exit 1
    fi

    # JUCE's LV2 host loads a bundle only from an absolute path.
    bundle="$(realpath "$bundle")"
    echo "Validating $bundle at strictness $strictness"
    "$pluginval" --strictness-level "$strictness" --validate-in-process \
        ${PLUGINVAL_EXTRA_ARGS:-} --timeout-ms 600000 --validate "$bundle"
done
