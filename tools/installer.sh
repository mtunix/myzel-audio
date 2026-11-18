#!/usr/bin/env bash
# Build one OS's suite installer from a stage directory (tools/stage.sh fills it).
# Usage: tools/installer.sh <linux|windows|macos> <stage-dir> [out-dir]
#   Run on the matching OS (Linux can also build the Windows installer under Wine).
#   out-dir defaults to dist/.
set -euo pipefail

. "$(cd "$(dirname "$0")/.." && pwd)/installer/common.sh"

os="${1:?usage: tools/installer.sh <linux|windows|macos> <stage-dir> [out-dir]}"
stage_dir="${2:?usage: tools/installer.sh <linux|windows|macos> <stage-dir> [out-dir]}"
out_dir="${3:-$MYZEL_ROOT/dist}"

case "$os" in
    linux | windows | macos) ;;
    *) echo "Unknown OS '$os' (expected linux, windows or macos)." >&2; exit 1 ;;
esac

for id in $(myzel_plugin_ids); do
    # Ids become Inno component names and pkg identifiers; this is the set all three accept.
    case "$id" in
        [a-z]*) ;;
        *) echo "Plugin id '$id' in installer/plugins.tsv must start with a lower-case letter." >&2; exit 1 ;;
    esac
    case "$id" in
        *[!a-z0-9]*) echo "Plugin id '$id' in installer/plugins.tsv: use lower-case letters and digits only." >&2; exit 1 ;;
    esac
    if [ ! -f "$stage_dir/$id/version" ]; then
        echo "$id is in installer/plugins.tsv but not staged in $stage_dir (tools/stage.sh)." >&2
        exit 1
    fi
done

mkdir -p "$out_dir"
stage_dir="$(cd "$stage_dir" && pwd)"
out_dir="$(cd "$out_dir" && pwd)"
exec bash "$MYZEL_ROOT/installer/$os/build.sh" "$stage_dir" "$out_dir"
