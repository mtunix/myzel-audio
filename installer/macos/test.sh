#!/bin/bash
# Install tests 1-6 for the macOS pkg (installer/PLAN.md, Install tests in CI), on a CI runner.
# Usage: installer/macos/test.sh <pkg> <stage-dir>
#   Installs into the real /Library and /Applications with sudo (passwordless on the runner) and
#   removes everything again; needs a Mac with no Myzel Audio plugins installed. PLUGINVAL=<binary>
#   also validates the installed VST3s (PLUGINVAL_STRICTNESS, default 10).
set -euo pipefail

here="$(cd "$(dirname "$0")" && pwd)"
. "$here/../common.sh"
. "$here/formats.sh"

pkg_in="${1:?usage: installer/macos/test.sh <pkg> <stage-dir>}"
stage="${2:?usage: installer/macos/test.sh <pkg> <stage-dir>}"
support_dir="/Library/Application Support/Myzel Audio"
uninstaller="$support_dir/uninstall.sh"
apps_dir="$(myzel_mac_dir Standalone)"
userdata="$HOME/Library/Application Support/Myzel Audio"
failures=0

die()
{
    echo "FAIL $*" >&2
    exit 1
}

step()
{
    echo
    echo "== $*"
}

pass()
{
    echo "ok   $*"
}

fail()
{
    echo "FAIL $*" >&2
    failures=$((failures + 1))
}

[ "$(uname -s)" = Darwin ] || die "The pkg tests run on macOS."
[ -f "$pkg_in" ] || die "No pkg at $pkg_in."
sudo -n true 2>/dev/null || die "The tests need passwordless sudo (as on the CI runner)."
command -v python3 >/dev/null || die "python3 is needed to read the installer's choice lists."

ids="$(myzel_plugin_ids | tr '\n' ' ')"
set -- $ids
[ $# -ge 3 ] || die "The tests need three plugins in installer/plugins.tsv (found: $ids)."
a="$1"
b="$2"
c="$3"

work="$(mktemp -d "${TMPDIR:-/tmp}/myzel-pkg-test.XXXXXX")"
sentinel=""
cleanup()
{
    if [ -n "$sentinel" ]; then
        rm -f "$sentinel"
    fi
    rm -rf "$work"
}
trap cleanup EXIT

name_of()
{
    myzel_plugin_name "$1"
}

version_of()
{
    myzel_stage_version "$stage" "$1"
}

formats_of()
{
    myzel_mac_stage_formats "$stage" "$1"
}

bundle_of()
{
    echo "$(myzel_mac_dir "$2")/$(name_of "$1")$(myzel_mac_ext "$2")"
}

receipt_of()
{
    echo "com.myzelaudio.pkg.$1.$(myzel_mac_key "$2")"
}

receipt_field()
{
    pkgutil --pkg-info "$1" 2>/dev/null | sed -n "s/^$2: //p" || true
}

plist_version()
{
    /usr/libexec/PlistBuddy -c 'Print :CFBundleShortVersionString' "$1/Contents/Info.plist" 2>/dev/null || true
}

our_receipts()
{
    pkgutil --pkgs 2>/dev/null | grep '^com\.myzelaudio\.' | sort || true
}

inode_of()
{
    stat -f %i "$1" 2>/dev/null || echo none
}

# AU and VST3 when staged (they have validators), else the first two formats.
two_formats()
{
    local formats picked="" format
    formats=" $(formats_of "$1" | tr '\n' ' ')"
    for format in AU VST3; do
        case "$formats " in
            *" $format "*) picked="$picked $format" ;;
        esac
    done
    if [ "$(echo $picked | wc -w | tr -d ' ')" -lt 2 ]; then
        picked="$(echo $formats | cut -d' ' -f1-2)"
    fi
    echo $picked
}

# Everything an install can add, two levels deep, plus the support folder.
snapshot()
{
    {
        find /Library/Audio/Plug-Ins -maxdepth 2 2>/dev/null || true
        find /Applications -maxdepth 2 2>/dev/null || true
        if [ -e "$support_dir" ]; then
            find "$support_dir" 2>/dev/null || true
        fi
    } | sort
}

# Folders an install may create that removal leaves, since other vendors share them.
is_shared_folder()
{
    case "$1" in
        "$(myzel_mac_dir AU)" | "$(myzel_mac_dir VST3)" | "$(myzel_mac_dir CLAP)") return 0 ;;
    esac
    return 1
}

# write_choices <file> <install spec> [remove ids]; install spec: "id=FORMAT,FORMAT id=FORMAT".
write_choices()
{
    local file="$1" spec="$2" removes="${3:-}" id format item wanted selected
    {
        echo '<?xml version="1.0" encoding="UTF-8"?>'
        echo '<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">'
        echo '<plist version="1.0">'
        echo '<array>'
        for id in $ids; do
            wanted=""
            for item in $spec; do
                if [ "${item%%=*}" = "$id" ]; then
                    wanted=",${item#*=},"
                fi
            done
            selected=1
            if [ -z "$wanted" ]; then
                selected=0
            fi
            choice_entry "install.$id" "$selected"
            for format in $(formats_of "$id"); do
                case "$wanted" in
                    *",$format,"*) selected=1 ;;
                    *) selected=0 ;;
                esac
                choice_entry "install.$id.$(myzel_mac_key "$format")" "$selected"
            done
            case " $removes " in
                *" $id "*) selected=1 ;;
                *) selected=0 ;;
            esac
            choice_entry "remove.$id" "$selected"
        done
        echo '</array>'
        echo '</plist>'
    } > "$file"
}

choice_entry()
{
    printf '<dict><key>choiceIdentifier</key><string>%s</string><key>choiceAttribute</key><string>selected</string><key>attributeSetting</key><integer>%s</integer></dict>\n' "$1" "$2"
}

# Prints which install and remove choices end up selected, for the log (best effort).
show_selection()
{
    installer -pkg "$pkg" -target / -showChoicesAfterApplyingChangesXML "$1" > "$work/after.plist" 2>/dev/null || return 0
    python3 -I - "$work/after.plist" <<'EOF' || true
import plistlib, sys
with open(sys.argv[1], 'rb') as f:
    data = plistlib.load(f)
picked = []
def walk(node):
    if isinstance(node, dict):
        if node.get('choiceIdentifier') and node.get('choiceIsSelected') in (1, True) and not node.get('childItems'):
            picked.append(node['choiceIdentifier'])
        for value in node.values():
            walk(value)
    elif isinstance(node, list):
        for value in node:
            walk(value)
walk(data)
print('     selected: ' + (', '.join(picked) if picked else '(none reported)'))
EOF
}

install_with()
{
    local label="$1" file="$work/choices-$2.plist"
    write_choices "$file" "$3" "${4:-}"
    echo "Installing ($label)"
    show_selection "$file"
    if ! sudo installer -pkg "$pkg" -target / -applyChoiceChangesXML "$file"; then
        tail -n 80 /var/log/install.log >&2 || true
        die "installer failed ($label)."
    fi
}

defaults_now()
{
    installer -pkg "$pkg" -target / -showChoiceChangesXML > "$work/defaults.plist" 2>/dev/null \
        || sudo installer -pkg "$pkg" -target / -showChoiceChangesXML > "$work/defaults.plist" \
        || die "installer -showChoiceChangesXML failed."
}

choice_attr()
{
    python3 -I - "$work/defaults.plist" "$1" "$2" <<'EOF'
import plistlib, sys
path, choice, attribute = sys.argv[1:4]
with open(path, 'rb') as f:
    items = plistlib.load(f)
for item in items:
    if item.get('choiceIdentifier') == choice and item.get('choiceAttribute') == attribute:
        value = item.get('attributeSetting')
        print(int(value) if isinstance(value, (bool, int)) else value)
        break
else:
    print('missing')
EOF
}

expect_choice()
{
    local got
    got="$(choice_attr "$1" "$2")"
    # installer may leave hidden choices out of the list altogether.
    if [ "$got" = missing ] && [ "$2" = visible ] && [ "$3" = 0 ]; then
        got=0
    fi
    if [ "$got" = "$3" ]; then
        pass "$4 ($1 $2 = $3)"
    else
        fail "$4: $1 $2 is $got, expected $3"
        if [ "$got" = missing ]; then
            cat "$work/defaults.plist" >&2
        fi
    fi
}

check_installed()
{
    local id="$1" wanted=" $2 " version format path receipt
    version="$(version_of "$id")"
    for format in $(formats_of "$id"); do
        path="$(bundle_of "$id" "$format")"
        receipt="$(receipt_of "$id" "$format")"
        case "$wanted" in
            *" $format "*)
                if [ -d "$path" ]; then pass "$path installed"; else fail "$path missing"; fi
                if [ "$(receipt_field "$receipt" version)" = "$version" ]; then
                    pass "receipt $receipt $version"
                else
                    fail "receipt $receipt: version '$(receipt_field "$receipt" version)', expected $version"
                fi
                if [ "$(plist_version "$path")" != "$version" ]; then
                    fail "$path carries version '$(plist_version "$path")', expected $version"
                fi
                ;;
            *)
                if [ -e "$path" ]; then fail "$path installed but not ticked"; fi
                if pkgutil --pkg-info "$receipt" >/dev/null 2>&1; then fail "receipt $receipt written but not ticked"; fi
                ;;
        esac
    done
}

check_gone()
{
    local id="$1" format path left
    for format in $MYZEL_MAC_FORMATS; do
        path="$(bundle_of "$id" "$format")"
        if [ -e "$path" ]; then fail "$path still there"; fi
    done
    left="$(pkgutil --pkgs 2>/dev/null | grep "^com\.myzelaudio\.pkg\.$id\." | grep -v '\.remove$' || true)"
    if [ -n "$left" ]; then
        fail "receipts of $id left: $(echo $left)"
    else
        pass "$id removed: no bundles, no receipts"
    fi
}

check_quarantine()
{
    local flagged
    flagged="$(find "$1" -exec sh -c 'xattr -p com.apple.quarantine "$1" >/dev/null 2>&1 && echo "$1"' _ {} \; 2>/dev/null || true)"
    if [ -z "$flagged" ]; then
        pass "no quarantine flag in $1"
    else
        fail "quarantine flag on: $(echo "$flagged" | head -n 5)"
    fi
}

check_codesign()
{
    if codesign --verify --strict --deep --verbose=2 "$1" > "$work/codesign.log" 2>&1; then
        pass "codesign --verify --strict $1"
    else
        cat "$work/codesign.log" >&2
        fail "codesign --verify --strict $1"
    fi
}

# auval finds a component by type, subtype and manufacturer; the registry can take a moment.
check_auval()
{
    local name="$1" triple="" try
    for try in 1 2 3 4 5 6 7 8 9 10; do
        auval -a > "$work/auval-list.txt" 2>/dev/null || true
        triple="$(awk -v suffix=": $name" '$3 == "Myzl" && substr($0, length($0) - length(suffix) + 1) == suffix { print $1, $2, $3; exit }' "$work/auval-list.txt")"
        [ -n "$triple" ] && break
        sleep 3
    done
    if [ -z "$triple" ]; then
        grep Myzl "$work/auval-list.txt" >&2 || true
        fail "auval -a does not list $name"
        return
    fi
    if auval -v $triple > "$work/auval.log" 2>&1 && grep -q 'AU VALIDATION SUCCEEDED' "$work/auval.log"; then
        pass "auval -v $triple ($name)"
    else
        tail -n 40 "$work/auval.log" >&2
        fail "auval -v $triple ($name)"
    fi
}

check_pluginval()
{
    [ -n "${PLUGINVAL:-}" ] || return 0
    if "$PLUGINVAL" --strictness-level "${PLUGINVAL_STRICTNESS:-10}" --validate-in-process \
        --timeout-ms 600000 --validate "$1" > "$work/pluginval.log" 2>&1; then
        pass "pluginval (strictness ${PLUGINVAL_STRICTNESS:-10}) $1"
    else
        tail -n 60 "$work/pluginval.log" >&2
        fail "pluginval $1"
    fi
}

# validate <id> <formats> [full]: the installed bundles load from where the pkg put them.
validate()
{
    local id="$1" format path
    for format in $2; do
        path="$(bundle_of "$id" "$format")"
        check_codesign "$path"
        if [ "${3:-}" = full ]; then
            check_quarantine "$path"
        fi
        if [ "$format" = AU ]; then
            check_auval "$(name_of "$id")"
        fi
        if [ "$format" = VST3 ] && [ "${3:-}" = full ]; then
            check_pluginval "$path"
        fi
    done
}

inodes_of()
{
    local id="$1" format
    for format in $2; do
        echo "$format $(inode_of "$(bundle_of "$id" "$format")") $(receipt_field "$(receipt_of "$id" "$format")" install-time)"
    done
}

all_c="$(formats_of "$c" | tr '\n' ' ')"
fmt_a="$(two_formats "$a")"
fmt_b="$(two_formats "$b")"

step "Clean machine"
for id in $ids; do
    for format in $MYZEL_MAC_FORMATS; do
        if [ -e "$(bundle_of "$id" "$format")" ]; then
            die "$(bundle_of "$id" "$format") exists; run uninstall.sh first."
        fi
    done
done
if [ -n "$(our_receipts)" ]; then
    die "Myzel Audio receipts exist: $(our_receipts | tr '\n' ' '); run uninstall.sh first."
fi
if [ -e "$support_dir" ]; then
    die "$support_dir exists; run uninstall.sh first."
fi
snapshot > "$work/before.txt"
mkdir -p "$userdata/$(name_of "$a")"
sentinel="$userdata/$(name_of "$a")/installer-test-sentinel.txt"
echo "user data the installer must not touch" > "$sentinel"
pass "nothing installed; user data sentinel at $sentinel"

step "Gatekeeper: a downloaded pkg"
pkg="$work/$(basename "$pkg_in")"
cp "$pkg_in" "$pkg"
xattr -w com.apple.quarantine "0083;$(printf '%x' "$(date +%s)");Safari;$(uuidgen)" "$pkg"
if spctl --assess --type install -vv "$pkg" > "$work/spctl.log" 2>&1; then
    cat "$work/spctl.log"
    if spctl --status 2>/dev/null | grep -q disabled; then
        echo "note Gatekeeper assessments are disabled on this machine; rejection not checked"
    else
        fail "spctl accepted the unsigned, quarantined pkg"
    fi
else
    cat "$work/spctl.log"
    pass "spctl rejects the quarantined pkg, as users will see (Open Anyway)"
fi

step "1. Fresh install: $a ($fmt_a) and $b ($fmt_b)"
install_with "two plugins, two formats" fresh "$a=$(echo $fmt_a | tr ' ' ,) $b=$(echo $fmt_b | tr ' ' ,)"
check_installed "$a" "$fmt_a"
check_installed "$b" "$fmt_b"
check_gone "$c"
if [ -x "$uninstaller" ]; then pass "$uninstaller installed"; else fail "$uninstaller missing or not executable"; fi
expected="com.myzelaudio.pkg.support"
for format in $fmt_a; do expected="$expected$(printf '\n%s' "$(receipt_of "$a" "$format")")"; done
for format in $fmt_b; do expected="$expected$(printf '\n%s' "$(receipt_of "$b" "$format")")"; done
if [ "$(our_receipts)" = "$(printf '%s\n' "$expected" | sort)" ]; then
    pass "receipts: exactly the ticked components and the uninstaller"
else
    fail "receipts are: $(our_receipts | tr '\n' ' '); expected: $(printf '%s\n' "$expected" | sort | tr '\n' ' ')"
fi
snapshot > "$work/after-fresh.txt"
allowed="$(printf '%s\n' "$support_dir" "$uninstaller" "$apps_dir")"
for format in $fmt_a; do allowed="$allowed$(printf '\n%s' "$(bundle_of "$a" "$format")")"; done
for format in $fmt_b; do allowed="$allowed$(printf '\n%s' "$(bundle_of "$b" "$format")")"; done
unexpected=""
while IFS= read -r added; do
    [ -n "$added" ] || continue
    if printf '%s\n' "$allowed" | grep -qxF -- "$added" || is_shared_folder "$added"; then
        continue
    fi
    unexpected="$unexpected $added"
done <<EOF
$(comm -13 "$work/before.txt" "$work/after-fresh.txt")
EOF
if [ -z "$unexpected" ]; then pass "nothing else written"; else fail "unexpected new files:$unexpected"; fi

step "2. Detection"
defaults_now
expect_choice "install.$a" selected 0 "$a up to date: unticked"
expect_choice "install.$b" selected 0 "$b up to date: unticked"
expect_choice "install.$c" selected 1 "$c not installed: ticked"
expect_choice "remove.$a" visible 1 "$a installed: Remove row shown"
expect_choice "remove.$c" visible 0 "$c not installed: no Remove row"
expect_choice "remove.$a" selected 0 "Remove rows start unticked"

fake_format=VST3
case " $all_c " in *" VST3 "*) ;; *) fake_format="$(echo $all_c | cut -d' ' -f1)" ;; esac
fake_path="$(bundle_of "$c" "$fake_format")"
fake_root="$work/fake-root"
mkdir -p "$fake_root/$(basename "$fake_path")/Contents"
cat > "$fake_root/$(basename "$fake_path")/Contents/Info.plist" <<EOF
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>CFBundleIdentifier</key><string>com.myzelaudio.installer-test.fake</string>
    <key>CFBundlePackageType</key><string>BNDL</string>
    <key>CFBundleShortVersionString</key><string>0.0.1</string>
</dict>
</plist>
EOF
pkgbuild --root "$fake_root" --identifier "$(receipt_of "$c" "$fake_format")" --version 0.0.1 \
    --install-location "$(myzel_mac_dir "$fake_format")" "$work/fake-older.pkg" >/dev/null
sudo installer -pkg "$work/fake-older.pkg" -target / >/dev/null || die "Installing the fake older $c failed."
defaults_now
expect_choice "install.$c" selected 1 "$c older (receipt 0.0.1): ticked"
expect_choice "remove.$c" visible 1 "$c older: Remove row shown"
sudo pkgutil --forget "$(receipt_of "$c" "$fake_format")" >/dev/null
sudo plutil -replace CFBundleShortVersionString -string 99.0.0 "$fake_path/Contents/Info.plist"
defaults_now
expect_choice "install.$c" selected 0 "$c newer (hand copy, 99.0.0): unticked"
sudo plutil -remove CFBundleShortVersionString "$fake_path/Contents/Info.plist"
defaults_now
expect_choice "install.$c" selected 1 "$c hand copy of unknown version: ticked"
expect_choice "remove.$c" visible 1 "$c hand copy: Remove row shown"

step "3. Add $c ($all_c) over its hand copy"
before_a="$(inodes_of "$a" "$fmt_a")"
before_b="$(inodes_of "$b" "$fmt_b")"
install_with "third plugin" third "$c=$(echo $all_c | tr ' ' ,)"
check_installed "$c" "$all_c"
if [ "$(inodes_of "$a" "$fmt_a")" = "$before_a" ] && [ "$(inodes_of "$b" "$fmt_b")" = "$before_b" ]; then
    pass "$a and $b untouched"
else
    fail "$a or $b was rewritten by the third install"
fi

step "4. Installed bundles load from their place"
validate "$a" "$fmt_a" full
validate "$b" "$fmt_b" full
validate "$c" "$all_c" full
check_quarantine "$uninstaller"
[ -n "${PLUGINVAL:-}" ] || echo "note PLUGINVAL not set; pluginval skipped"

step "5a. Remove $a through the installer's Remove row"
before_b="$(inodes_of "$b" "$fmt_b")"
before_c="$(inodes_of "$c" "$all_c")"
install_with "remove $a" remove "" "$a"
check_gone "$a"
if [ "$(inodes_of "$b" "$fmt_b")" = "$before_b" ] && [ "$(inodes_of "$c" "$all_c")" = "$before_c" ]; then
    pass "$b and $c untouched"
else
    fail "$b or $c changed when removing $a"
fi
if pkgutil --pkg-info "com.myzelaudio.pkg.$a.remove" >/dev/null 2>&1; then
    echo "note the payload-free com.myzelaudio.pkg.$a.remove left a receipt (uninstall.sh forgets it)"
fi
validate "$b" "$fmt_b"
validate "$c" "$all_c"

step "5b. Remove $b with uninstall.sh"
sudo "$uninstaller" "$b" || fail "uninstall.sh $b exited non-zero"
check_gone "$b"
if [ "$(inodes_of "$c" "$all_c")" = "$before_c" ]; then pass "$c untouched"; else fail "$c changed when removing $b"; fi
sudo "$uninstaller" "$b" > /dev/null || fail "uninstall.sh $b is not idempotent"
validate "$c" "$all_c"

step "6. Full uninstall"
sudo "$uninstaller" || fail "uninstall.sh exited non-zero"
for id in $ids; do
    check_gone "$id"
done
if [ -z "$(our_receipts)" ]; then pass "no Myzel Audio receipts left"; else fail "receipts left: $(our_receipts | tr '\n' ' ')"; fi
if [ -e "$support_dir" ]; then fail "$support_dir left"; else pass "$support_dir removed"; fi
if [ -e "$apps_dir" ]; then fail "$apps_dir left"; else pass "$apps_dir removed"; fi
snapshot > "$work/after-all.txt"
left=""
while IFS= read -r added; do
    [ -n "$added" ] || continue
    is_shared_folder "$added" || left="$left $added"
done <<EOF
$(comm -13 "$work/before.txt" "$work/after-all.txt")
EOF
if [ -z "$left" ]; then pass "everything the installer wrote is gone"; else fail "left behind:$left"; fi
if [ "$(cat "$sentinel" 2>/dev/null)" = "user data the installer must not touch" ]; then
    pass "user data untouched"
else
    fail "user data sentinel $sentinel changed or removed"
fi

echo
if [ "$failures" -gt 0 ]; then
    echo "$failures check(s) failed." >&2
    exit 1
fi
echo "All macOS install tests passed."
