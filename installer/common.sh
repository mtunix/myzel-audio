# Helpers shared by the installer build scripts (installer/<os>/build.sh) and tools/stage.sh.
# Sourced, never run. Bash 3.2 compatible: macOS runs its build with /bin/bash.

MYZEL_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
MYZEL_PLUGINS_TSV="$MYZEL_ROOT/installer/plugins.tsv"

myzel_suite_version()
{
    tr -d '[:space:]' < "$MYZEL_ROOT/installer/VERSION"
}

# Prints "id<TAB>name<TAB>blurb" per shipped plugin.
myzel_plugins()
{
    grep -v '^#' "$MYZEL_PLUGINS_TSV" | grep -v '^[[:space:]]*$'
}

myzel_plugin_ids()
{
    myzel_plugins | cut -f1
}

myzel_plugin_name()
{
    myzel_plugins | awk -F'\t' -v id="$1" '$1 == id { print $2 }'
}

myzel_plugin_blurb()
{
    myzel_plugins | awk -F'\t' -v id="$1" '$1 == id { print $3 }'
}

# The version in the plugin's project() line, e.g. 0.1.0.
myzel_plugin_source_version()
{
    local name
    name="$(myzel_plugin_name "$1")"
    sed -n "s/^project($name VERSION \([0-9.]*\).*/\1/p" "$MYZEL_ROOT/plugins/$1/CMakeLists.txt"
}

# The version recorded in a stage directory (tools/stage.sh writes it).
myzel_stage_version()
{
    tr -d '[:space:]' < "$1/$2/version"
}

# Formats present in a plugin's stage, one per line (AU, CLAP, LV2, Standalone, VST3).
myzel_stage_formats()
{
    local dir
    for dir in "$1/$2"/*/; do
        [ -d "$dir" ] && basename "$dir"
    done
    # An unmatched glob leaves the last test false; callers assign this under set -e.
    return 0
}

myzel_host_os()
{
    case "$(uname -s)" in
        Linux) echo linux ;;
        Darwin) echo macos ;;
        MINGW* | MSYS* | CYGWIN*) echo windows ;;
        *) echo unknown ;;
    esac
}
