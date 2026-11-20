#!/bin/bash
# Removes Myzel Audio plugins from this Mac: every format of each, and the installer's receipts.
# Usage: sudo "/Library/Application Support/Myzel Audio/uninstall.sh" [plugin...]
#   With no plugin named, removes every plugin and this uninstaller. Finds plugins by the pkg receipts
#   and by bundle name, so hand-copied bundles go too. Presets and settings in
#   ~/Library/Application Support/Myzel Audio are never touched.
set -u

support_dir="/Library/Application Support/Myzel Audio"
apps_dir="/Applications/Myzel Audio"
support_receipt="com.myzelaudio.pkg.support"

# "id<TAB>Name" per plugin this copy knows; build.sh fills it from installer/plugins.tsv.
known_plugins()
{
    cat <<'PLUGINS'
@MYZEL_PLUGINS@
PLUGINS
}

# "folder<TAB>extension" per format: the table in installer/macos/formats.sh.
format_table()
{
    printf '%s\t%s\n' \
        "/Library/Audio/Plug-Ins/Components" ".component" \
        "/Library/Audio/Plug-Ins/VST3" ".vst3" \
        "/Library/Audio/Plug-Ins/CLAP" ".clap" \
        "$apps_dir" ".app"
}

known_ids()
{
    known_plugins | awk -F'\t' 'NF >= 2 && $1 != "" { print $1 }'
}

name_of()
{
    known_plugins | awk -F'\t' -v id="$1" 'NF >= 2 && $1 == id { print $2 }'
}

plugin_receipts()
{
    pkgutil --pkgs 2>/dev/null | grep "^com\.myzelaudio\.pkg\.$1\." || true
}

receipt_ids()
{
    pkgutil --pkgs 2>/dev/null | awk -F. '/^com\.myzelaudio\.pkg\./ && NF >= 5 { print $4 }' | sort -u
}

failures=0
removed=0
removed_au=0

remove_path()
{
    if [ -e "$1" ] || [ -L "$1" ]; then
        if rm -rf "$1"; then
            echo "Removed $1"
            removed=$((removed + 1))
            case "$1" in *.component) removed_au=1 ;; esac
        else
            echo "Could not remove $1" >&2
            failures=$((failures + 1))
        fi
    fi
}

forget_receipt()
{
    if pkgutil --forget "$1" >/dev/null 2>&1; then
        echo "Forgot receipt $1"
    else
        echo "Could not forget receipt $1" >&2
        failures=$((failures + 1))
    fi
}

# The bundles a receipt installed: its top-level entries under the receipt's volume and location.
remove_receipt_files()
{
    local info volume location base entry entries
    info="$(pkgutil --pkg-info "$1" 2>/dev/null)" || return 0
    volume="$(printf '%s\n' "$info" | sed -n 's/^volume: //p')"
    location="$(printf '%s\n' "$info" | sed -n 's/^location: //p')"
    base="${volume:-/}"
    base="${base%/}/${location#/}"
    base="${base%/}"
    entries="$(pkgutil --files "$1" 2>/dev/null | grep -v / || true)"
    while IFS= read -r entry; do
        case "$entry" in
            *.component | *.vst3 | *.clap | *.app) remove_path "$base/$entry" ;;
        esac
    done <<EOF
$entries
EOF
}

remove_plugin()
{
    local id="$1" name receipt receipts folder ext before="$removed"
    name="$(name_of "$id")"
    receipts="$(plugin_receipts "$id")"

    for receipt in $receipts; do
        remove_receipt_files "$receipt"
    done
    if [ -n "$name" ]; then
        while IFS="$(printf '\t')" read -r folder ext; do
            remove_path "$folder/$name$ext"
        done <<EOF
$(format_table)
EOF
    fi
    for receipt in $receipts; do
        forget_receipt "$receipt"
    done

    if [ "$all" = 0 ] && [ "$removed" = "$before" ] && [ -z "$receipts" ]; then
        echo "${name:-$id}: nothing installed."
    fi
}

# Drops the Standalone folder once it holds nothing but Finder's .DS_Store.
tidy_apps_dir()
{
    [ -d "$apps_dir" ] || return 0
    if [ -z "$(ls -A "$apps_dir" | grep -v '^\.DS_Store$')" ]; then
        rm -f "$apps_dir/.DS_Store"
        rmdir "$apps_dir" && echo "Removed $apps_dir"
    fi
}

if [ "$(id -u)" -ne 0 ]; then
    echo "Removing plugins needs administrator rights: sudo \"$0\" $*" >&2
    exit 1
fi

all=0
targets=""
if [ $# -eq 0 ]; then
    all=1
    targets="$( (known_ids; receipt_ids) | sort -u)"
else
    known="$( (known_ids; receipt_ids) | sort -u)"
    for arg in "$@"; do
        id="$(printf '%s' "$arg" | tr '[:upper:]' '[:lower:]')"
        if ! printf '%s\n' "$known" | grep -qxF -- "$id"; then
            echo "Unknown plugin '$arg'. Known: $(printf '%s ' $known)" >&2
            exit 2
        fi
        targets="$targets $id"
    done
fi

for id in $targets; do
    remove_plugin "$id"
done

tidy_apps_dir

if [ "$all" = 1 ]; then
    # Receipts left by packages this copy does not know, e.g. a .remove package's own receipt.
    for receipt in $(pkgutil --pkgs 2>/dev/null | grep '^com\.myzelaudio\.pkg\.' | grep -vxF "$support_receipt"); do
        forget_receipt "$receipt"
    done
    remove_path "$support_dir/uninstall.sh"
    remove_path "$support_dir/LICENSES.txt"
    if [ -d "$support_dir" ] && rmdir "$support_dir" 2>/dev/null; then
        echo "Removed $support_dir"
    fi
    if pkgutil --pkgs 2>/dev/null | grep -qxF "$support_receipt"; then
        forget_receipt "$support_receipt"
    fi
fi

if [ "$removed_au" = 1 ]; then
    killall -9 AudioComponentRegistrar >/dev/null 2>&1 || true
fi

if [ "$failures" -gt 0 ]; then
    echo "$failures item(s) could not be removed." >&2
    exit 1
fi
echo "Done. Rescan plugins in your host."
