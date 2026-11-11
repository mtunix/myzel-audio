#!/usr/bin/env bash
# Build a plugin's Linux release tarball: VST3, CLAP, LV2 and Standalone, plus an install script.
# Usage: tools/package.sh <plugin> [build-dir]
#   plugin: cerulean, umber, ... (a directory under plugins/)
#   With no build dir, configures and builds a fresh LTO release of that plugin in build-release-<plugin>/.
set -euo pipefail

root="$(cd "$(dirname "$0")/.." && pwd)"
plugin="${1:?usage: tools/package.sh <plugin> [build-dir]}"
build_dir="${2:-}"
plugin_dir="$root/plugins/$plugin"

if [[ ! -d "$plugin_dir" ]]; then
    echo "Unknown plugin '$plugin' (expected a directory under plugins/)." >&2
    exit 1
fi
name="${plugin^}"

if [[ -z "$build_dir" ]]; then
    build_dir="$root/build-release-$plugin"
    cmake -S "$root" -B "$build_dir" -G Ninja -DCMAKE_BUILD_TYPE=Release -DMYZEL_PLUGINS="$plugin" \
        -DMYZEL_ENABLE_LTO=ON -DMYZEL_BUILD_TESTS=OFF -DMYZEL_BUILD_TOOLS=OFF
    cmake --build "$build_dir" --target "${name}_VST3" "${name}_CLAP" "${name}_LV2" "${name}_Standalone"
fi

version="$(sed -n "s/^project($name VERSION \([0-9.]*\).*/\1/p" "$plugin_dir/CMakeLists.txt")"
arch="$(uname -m)"
package="${name}-${version}-linux-${arch}"
artefacts="$build_dir/plugins/$plugin/${name}_artefacts/Release"
stage="$root/dist/$package"

rm -rf "$stage"
mkdir -p "$stage"
cp -r "$artefacts/VST3/$name.vst3" "$stage/"
cp -r "$artefacts/LV2/$name.lv2" "$stage/"
cp "$artefacts/CLAP/$name.clap" "$stage/"
cp "$artefacts/Standalone/$name" "$stage/"
cp "$root/LICENSE" "$plugin_dir/README.md" "$stage/"
mkdir -p "$stage/docs"
cp "$plugin_dir/docs/MANUAL.md" "$stage/docs/" 2>/dev/null || true

cat > "$stage/install.sh" <<INSTALL
#!/usr/bin/env bash
# Installs $name by Myzel Audio for the current user (no root needed).
# Usage: ./install.sh            install
#        ./install.sh --remove   uninstall
set -euo pipefail
here="\$(cd "\$(dirname "\$0")" && pwd)"

if [[ "\${1:-}" == "--remove" ]]; then
    rm -rf ~/.vst3/$name.vst3 ~/.lv2/$name.lv2 ~/.clap/$name.clap ~/.local/bin/$name
    echo "$name removed."
    exit 0
fi

mkdir -p ~/.vst3 ~/.lv2 ~/.clap ~/.local/bin
# Remove first, so a host that has the old binary loaded keeps its copy instead of crashing.
rm -rf ~/.vst3/$name.vst3 ~/.lv2/$name.lv2 ~/.clap/$name.clap ~/.local/bin/$name
cp -r "\$here/$name.vst3" ~/.vst3/
cp -r "\$here/$name.lv2" ~/.lv2/
cp "\$here/$name.clap" ~/.clap/
cp "\$here/$name" ~/.local/bin/
echo "Installed: ~/.vst3/$name.vst3, ~/.lv2/$name.lv2, ~/.clap/$name.clap, ~/.local/bin/$name"
echo "Rescan plugins in your host."
INSTALL
chmod +x "$stage/install.sh"

tar -C "$root/dist" -czf "$root/dist/$package.tar.gz" "$package"
echo "dist/$package.tar.gz"
