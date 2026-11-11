#!/usr/bin/env bash
# Validate a built plugin's VST3 with Tracktion's pluginval.
# Usage: tools/pluginval.sh <plugin> [build-dir] [strictness 1-10]
#   plugin: cerulean, umber, ... (a directory under plugins/)
set -euo pipefail

root="$(cd "$(dirname "$0")/.." && pwd)"
plugin="${1:?usage: tools/pluginval.sh <plugin> [build-dir] [strictness]}"
build_dir="${2:-$root/build}"
strictness="${3:-10}"
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

vst3="$(find "$build_dir" -type d -name "$name.vst3" -path '*VST3*' | head -n 1)"
if [[ -z "$vst3" ]]; then
    echo "$name.vst3 not found under $build_dir. Build first." >&2
    exit 1
fi

echo "Validating $vst3 at strictness $strictness"
"$pluginval" --strictness-level "$strictness" --validate-in-process \
    ${PLUGINVAL_EXTRA_ARGS:-} --timeout-ms 600000 --validate "$vst3"
