#!/usr/bin/env bash
# Copy one plugin's built formats into an installer stage directory (layout: installer/PLAN.md, Stage).
# Usage: tools/stage.sh <plugin> <build-dir> <stage-dir>
#   Run on the OS the plugin was built for; takes the Release artefacts of that build dir.
set -euo pipefail

. "$(cd "$(dirname "$0")/.." && pwd)/installer/common.sh"

plugin="${1:?usage: tools/stage.sh <plugin> <build-dir> <stage-dir>}"
build_dir="${2:?usage: tools/stage.sh <plugin> <build-dir> <stage-dir>}"
stage_dir="${3:?usage: tools/stage.sh <plugin> <build-dir> <stage-dir>}"

name="$(myzel_plugin_name "$plugin")"
if [ -z "$name" ]; then
    echo "'$plugin' is not in installer/plugins.tsv." >&2
    exit 1
fi

os="$(myzel_host_os)"
artefacts="$build_dir/plugins/$plugin/${name}_artefacts/Release"
out="$stage_dir/$plugin"

case "$os" in
    linux)   products="VST3/$name.vst3 CLAP/$name.clap LV2/$name.lv2 Standalone/$name" ;;
    windows) products="VST3/$name.vst3 CLAP/$name.clap Standalone/$name.exe" ;;
    macos)   products="AU/$name.component VST3/$name.vst3 CLAP/$name.clap Standalone/$name.app" ;;
    *) echo "Unsupported OS: $(uname -s)" >&2; exit 1 ;;
esac

rm -rf "$out"
mkdir -p "$out"
for product in $products; do
    if [ ! -e "$artefacts/$product" ]; then
        echo "Missing $artefacts/$product. Build the plugin first." >&2
        exit 1
    fi
    mkdir -p "$out/$(dirname "$product")"
    cp -R "$artefacts/$product" "$out/$product"
done

myzel_plugin_source_version "$plugin" > "$out/version"
echo "Staged $name $(cat "$out/version") ($os): $(myzel_stage_formats "$stage_dir" "$plugin" | tr '\n' ' ')"
