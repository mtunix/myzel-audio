#!/usr/bin/env bash
# Install tests 1-6 for the Linux .run (installer/PLAN.md, Install tests in CI), per-user and system-wide.
# Runs unprivileged: a temp HOME and MYZEL_SYSTEM_ROOT stand in for the user's home and /.
# Usage: installer/linux/test.sh <run-file> <stage-dir>
#   PLUGINVAL=<pluginval binary> also validates the installed VST3s (PLUGINVAL_STRICTNESS, default 10);
#   pluginval needs a display, so run the script under xvfb-run on a headless machine.
set -euo pipefail

. "$(dirname "$0")/../common.sh"

run_arg="${1:?usage: installer/linux/test.sh <run-file> <stage-dir>}"
stage_arg="${2:?usage: installer/linux/test.sh <run-file> <stage-dir>}"
run="$(cd "$(dirname "$run_arg")" && pwd)/$(basename "$run_arg")"
stage="$(cd "$stage_arg" && pwd)"
strictness="${PLUGINVAL_STRICTNESS:-10}"
suite="$(myzel_suite_version)"

# As root the pkexec/sudo checks would skip escalation and write to the real /usr.
if [ "$(id -u)" = 0 ]; then
    echo "Run the installer tests as a regular user, not root." >&2
    exit 1
fi

tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT
log="$tmp/log"

fail()
{
    echo "FAIL: $*" >&2
    if [ -s "$log" ]; then
        echo "---- output of the last command ----" >&2
        cat "$log" >&2
    fi
    exit 1
}

pass()
{
    echo "ok    $*"
}

# Runs a command with its output in $log; fails the test when it fails.
must()
{
    "$@" > "$log" 2>&1 || fail "exit $? from: $*"
}

must_not()
{
    if "$@" > "$log" 2>&1; then fail "expected a failure from: $*"; fi
}

log_has()
{
    grep -qF -- "$1" "$log" || fail "output lacks '$1'"
}

ids=()
while IFS= read -r id; do ids+=("$id"); done < <(myzel_plugin_ids)
[ ${#ids[@]} -ge 3 ] || fail "these tests need at least three plugins in installer/plugins.tsv"
A="${ids[0]}"
B="${ids[1]}"
C="${ids[2]}"
name_of()
{
    myzel_plugin_name "$1"
}
ver_of()
{
    myzel_stage_version "$stage" "$1"
}

must "$run" --quiet --noexec --target "$tmp/payload"
setup="$tmp/payload/setup.sh"
[ -f "$setup" ] || fail "the .run has no setup.sh"

# Spaces in both roots, because users have them.
home="$tmp/home dir"
root="$tmp/system root"
userdata="$home/.config/Myzel Audio/$(name_of "$A")/settings.xml"
export HOME="$home" MYZEL_SYSTEM_ROOT="$root" MYZEL_UI=text
unset XDG_DATA_HOME XDG_CONFIG_HOME

fresh()
{
    rm -rf "$home" "$root"
    mkdir -p "$(dirname "$userdata")" "$root"
    echo "<settings user=\"data\"/>" > "$userdata"
}

# Sets the expected folders for a scope (user or system) and the setup.sh flag that selects it.
use_scope()
{
    scope=$1
    if [ "$scope" = user ]; then
        label=per-user
        flag=()
        d_vst3="$home/.vst3"
        d_clap="$home/.clap"
        d_lv2="$home/.lv2"
        d_bin="$home/.local/bin"
        d_apps="$home/.local/share/applications"
        d_icons="$home/.local/share/icons/hicolor/scalable/apps"
        d_data="$home/.local/share/myzel-audio"
    else
        label=system-wide
        flag=(--system)
        d_vst3="$root/usr/local/lib/vst3"
        d_clap="$root/usr/lib/clap"
        d_lv2="$root/usr/local/lib/lv2"
        d_bin="$root/usr/local/bin"
        d_apps="$root/usr/local/share/applications"
        d_icons="$root/usr/local/share/icons/hicolor/scalable/apps"
        d_data="$root/usr/local/share/myzel-audio"
    fi
}

# Every path a plugin can own in the current scope, existing or not.
paths_of()
{
    local n
    n="$(name_of "$1")"
    printf '%s\n' "$d_vst3/$n.vst3" "$d_clap/$n.clap" "$d_lv2/$n.lv2" "$d_bin/$n" \
        "$d_apps/myzel-audio-$1.desktop" "$d_icons/myzel-audio-$1.svg" "$d_data/installed/$1"
}

# Files and links written anywhere under the test roots, except the user data folder.
written()
{
    find "$home" "$root" \( -type f -o -type l \) ! -path "$home/.config/*" | sort
}

# Only the given paths (files, or folders with everything below) were written, and all of them exist.
expect_only()
{
    local f p ok
    for p in "$@"; do
        [ -e "$p" ] || fail "missing: $p"
    done
    while IFS= read -r f; do
        ok=""
        for p in "$@"; do
            case "$f" in
                "$p" | "$p"/*) ok=1; break ;;
            esac
        done
        [ -n "$ok" ] || fail "written but not expected: $f"
    done < <(written)
}

user_data_intact()
{
    [ "$(cat "$userdata" 2> /dev/null)" = "<settings user=\"data\"/>" ] || fail "the user data file changed"
}

# Inode, mtime and size of everything a plugin owns: a reinstall changes them.
snapshot()
{
    local p
    paths_of "$1" | while IFS= read -r p; do
        if [ -e "$p" ]; then find "$p" -printf '%p %i %T@ %s\n'; fi
    done | sort
}

expect_status()
{
    local n line
    n="$(name_of "$1")"
    "$setup" --list > "$log" 2>&1 || fail "--list failed"
    line="$(grep -E "^$n +$label " "$log" || true)"
    [[ $line == *"$2"* ]] || fail "--list: expected '$2' for $n ($label), got: ${line:-nothing}"
}

expect_receipt()
{
    local r="$d_data/installed/$1"
    [ -f "$r" ] || fail "no receipt $r"
    grep -qx "version=$2" "$r" || fail "receipt $r: expected version=$2"
    grep -qx "suite=$suite" "$r" || fail "receipt $r: expected suite=$suite"
    grep -qx "formats=$3" "$r" || fail "receipt $r: expected formats=$3"
    grep -q '^file=' "$r" || fail "receipt $r lists no files"
    while IFS= read -r p; do
        [ -e "$p" ] || fail "receipt $r lists $p, which does not exist"
    done < <(sed -n 's/^file=//p' "$r")
}

expect_gone()
{
    local p
    while IFS= read -r p; do
        if [ -e "$p" ] || [ -L "$p" ]; then fail "still there: $p"; fi
    done < <(paths_of "$1")
}

pluginval_check()
{
    local bundle
    [ -n "${PLUGINVAL:-}" ] || return 0
    [ -x "$PLUGINVAL" ] || fail "PLUGINVAL=$PLUGINVAL is not executable"
    for bundle in "$@"; do
        echo "      pluginval (strictness $strictness): $bundle"
        # pluginval and the plugin write settings; keep them out of the test home.
        HOME="$tmp/pluginval-home" "$PLUGINVAL" --strictness-level "$strictness" --validate-in-process \
            --timeout-ms 600000 --validate "$bundle" > "$log" 2>&1 \
            || fail "pluginval failed for $bundle"
    done
}

scope_choice()
{
    if [ "$scope" = user ]; then echo 1; else echo 2; fi
}

test_scope()
{
    use_scope "$1"
    local nA nB nC vA vB vC before n toggles i
    nA="$(name_of "$A")"
    nB="$(name_of "$B")"
    nC="$(name_of "$C")"
    vA="$(ver_of "$A")"
    vB="$(ver_of "$B")"
    vC="$(ver_of "$C")"
    echo "== $label"
    fresh

    # 1. Fresh install of two plugins, two formats, through the .run itself.
    must "$run" --quiet -- --plugins "$A,$B" --formats vst3,clap --yes "${flag[@]}"
    expect_receipt "$A" "$vA" "VST3 CLAP"
    expect_receipt "$B" "$vB" "VST3 CLAP"
    expect_only "$d_vst3/$nA.vst3" "$d_clap/$nA.clap" "$d_vst3/$nB.vst3" "$d_clap/$nB.clap" \
        "$d_data/installed/$A" "$d_data/installed/$B" "$d_data/setup.sh" "$d_data/LICENSES.txt"
    [ -x "$d_data/setup.sh" ] || fail "the kept setup.sh is not executable"
    grep -qF "SIL Open Font License" "$d_data/LICENSES.txt" || fail "LICENSES.txt has no font licence"
    if [ "$scope" = system ] && [ -n "$(find "$root" -mindepth 1 ! -perm -o=r)" ]; then
        fail "system-wide files that other users cannot read"
    fi
    user_data_intact
    pass "1 fresh install: $A, $B (VST3, CLAP), receipts, nothing else written"

    # 2. Detection.
    expect_status "$A" "installed $vA (up to date)"
    expect_status "$B" "installed $vB (up to date)"
    expect_status "$C" "not installed"
    cp "$d_data/installed/$A" "$tmp/receipt"
    sed -i 's/^version=.*/version=0.0.1/' "$d_data/installed/$A"
    expect_status "$A" "installed 0.0.1 → updates to $vA"
    sed -i 's/^version=.*/version=999.0.0/' "$d_data/installed/$A"
    expect_status "$A" "installed 999.0.0 (newer)"
    must "$setup" --plugins "$A" --formats vst3,clap --yes "${flag[@]}"
    log_has "skipped"
    grep -qx "version=999.0.0" "$d_data/installed/$A" || fail "--yes replaced a newer version without --downgrade"
    must "$setup" --plugins "$A" --formats vst3,clap --yes --downgrade "${flag[@]}"
    expect_receipt "$A" "$vA" "VST3 CLAP"
    cmp -s <(grep -v '^file=' "$tmp/receipt") <(grep -v '^file=' "$d_data/installed/$A") \
        || fail "the downgrade wrote a different receipt"
    mkdir -p "$d_clap" "$d_vst3"
    cp -R "$stage/$C/CLAP/$nC.clap" "$d_clap/"
    expect_status "$C" "installed (unknown version) → replaces with $vC"
    cp -R "$stage/$C/VST3/$nC.vst3" "$d_vst3/"
    expect_status "$C" "installed $vC (up to date)"
    rm -rf "${d_vst3:?}/$nC.vst3"
    pass "2 detection: up to date, update, newer (skipped, then --downgrade), unknown version, moduleinfo.json"

    # 3. Re-run with the third plugin ticked: the text checklist ticks it by default (unknown version).
    # Plugins after the third are ticked by default too (not installed): untick them.
    n=${#ids[@]}
    input="$(scope_choice)"$'\n'
    for ((i = 4; i <= n; i++)); do input+="$i"$'\n'; done
    before="$(snapshot "$A")$(snapshot "$B")"
    must bash -c 'printf "%s\n\ny\n" "$1" | "$2"' _ "$input" "$setup"
    [ "$before" = "$(snapshot "$A")$(snapshot "$B")" ] || fail "re-run touched $A or $B"
    expect_receipt "$C" "$vC" "VST3 CLAP LV2 Standalone"
    [ -x "$d_bin/$nC" ] || fail "no Standalone $d_bin/$nC"
    if command -v desktop-file-validate > /dev/null 2>&1; then
        desktop-file-validate "$d_apps/myzel-audio-$C.desktop" > "$log" 2>&1 || fail "invalid .desktop entry"
    fi
    grep -qF "Exec=\"$d_bin/$nC\"" "$d_apps/myzel-audio-$C.desktop" || fail ".desktop Exec does not name $d_bin/$nC"
    expect_only "$d_vst3/$nA.vst3" "$d_clap/$nA.clap" "$d_vst3/$nB.vst3" "$d_clap/$nB.clap" \
        "$d_vst3/$nC.vst3" "$d_clap/$nC.clap" "$d_lv2/$nC.lv2" "$d_bin/$nC" \
        "$d_apps/myzel-audio-$C.desktop" "$d_icons/myzel-audio-$C.svg" \
        "$d_data/installed/$A" "$d_data/installed/$B" "$d_data/installed/$C" "$d_data/setup.sh" "$d_data/LICENSES.txt"
    pass "3 re-run (text checklist): $C added in every format, $A and $B untouched"

    # 4. pluginval against the installed VST3s (every plugin per-user, the first one system-wide).
    if [ -n "${PLUGINVAL:-}" ]; then
        if [ "$scope" = user ]; then
            pluginval_check "$d_vst3/$nA.vst3" "$d_vst3/$nB.vst3" "$d_vst3/$nC.vst3"
        else
            pluginval_check "$d_vst3/$nA.vst3"
        fi
        pass "4 pluginval on the installed VST3"
    else
        echo "skip  4 pluginval (PLUGINVAL not set)"
    fi

    # 5. Remove one plugin through the installer's checklist, one through the kept copy.
    toggles="$((n + 1))"
    for ((i = 4; i <= n; i++)); do toggles="$toggles $i"; done
    before="$(snapshot "$B")$(snapshot "$C")"
    must bash -c 'printf "%s\n%s\n\ny\n" "$1" "$2" | "$3"' _ "$(scope_choice)" "$toggles" "$setup"
    expect_gone "$A"
    [ "$before" = "$(snapshot "$B")$(snapshot "$C")" ] || fail "removing $A touched $B or $C"
    before="$(snapshot "$C")"
    must "$d_data/setup.sh" --remove "$B" --yes "${flag[@]}"
    expect_gone "$B"
    [ "$before" = "$(snapshot "$C")" ] || fail "removing $B touched $C"
    expect_receipt "$C" "$vC" "VST3 CLAP LV2 Standalone"
    pluginval_check "$d_vst3/$nC.vst3"
    pass "5 removal: $A through the checklist, $B through the kept setup.sh; $C intact"

    # 6. Full uninstall through the kept copy.
    must "$d_data/setup.sh" --remove-all --yes "${flag[@]}"
    [ -z "$(written)" ] || fail "left behind after --remove-all: $(written | head -n 5)"
    [ ! -e "$d_data" ] || fail "$d_data still exists"
    user_data_intact
    pass "6 full uninstall: nothing left, user data untouched"
}

test_extras()
{
    local nA vA fake b display opt before
    nA="$(name_of "$A")"
    vA="$(ver_of "$A")"
    echo "== both scopes, options, dialog back-ends"
    fresh

    use_scope user
    must "$setup" --plugins "$A" --formats vst3 --yes
    must "$setup" --plugins "$A" --formats vst3 --yes --system
    must "$setup" --list
    log_has "Note: $nA is installed in both scopes"
    must "$setup" --plugins "$A" --remove "$A" --yes
    log_has "removing, so not installing"
    expect_gone "$A"
    must "$setup" --remove-all --yes --system
    [ -z "$(written)" ] || fail "left behind: $(written | head -n 5)"
    pass "both scopes flagged in --list; removing wins over installing"

    must_not "$setup" --plugins nosuchplugin --yes
    log_has "not in this installer"
    must_not "$setup" --formats vst2 --yes
    cp "$tmp/payload/glibc" "$tmp/glibc"
    echo 99.0 > "$tmp/payload/glibc"
    must_not "$setup" --plugins "$A" --yes
    log_has "need glibc 99.0 or newer"
    cp "$tmp/glibc" "$tmp/payload/glibc"
    [ -z "$(written)" ] || fail "a refused run wrote files"
    pass "unknown plugin, unknown format and too old glibc are refused"

    XDG_DATA_HOME="$tmp/xdg data" must "$setup" --plugins "$A" --formats standalone --yes
    [ -f "$tmp/xdg data/applications/myzel-audio-$A.desktop" ] || fail "XDG_DATA_HOME: no .desktop entry there"
    [ -f "$tmp/xdg data/myzel-audio/installed/$A" ] || fail "XDG_DATA_HOME: no receipt there"
    XDG_DATA_HOME="$tmp/xdg data" must "$tmp/xdg data/myzel-audio/setup.sh" --remove-all --yes
    [ -z "$(find "$tmp/xdg data" -type f)" ] || fail "XDG_DATA_HOME: files left"
    pass "XDG_DATA_HOME holds receipts and the menu entry"

    # Fake dialogs log their arguments and answer from FAKE_*; the real ones need a display or a terminal.
    fake="$tmp/fakebin"
    mkdir -p "$fake"
    for b in zenity kdialog whiptail; do
        cat > "$fake/$b" <<'EOF'
#!/usr/bin/env bash
printf '%s\n' "$(basename "$0") $*" >> "$FAKE_LOG"
answer=""
case " $* " in
    *" --radiolist "*) answer="$FAKE_RADIO" ;;
    *" --checklist "*" i:"* | *" --checklist "*" r:"*) answer="$FAKE_PICK" ;;
    *" --checklist "*) answer="$FAKE_FORMATS" ;;
esac
if [ "$(basename "$0")" = whiptail ]; then printf '%s\n' "$answer" >&2; else printf '%s\n' "$answer"; fi
exit 0
EOF
        chmod +x "$fake/$b"
    done
    for b in zenity kdialog whiptail; do
        : > "$tmp/fake.log"
        PATH="$fake:$PATH" MYZEL_UI="$b" FAKE_LOG="$tmp/fake.log" FAKE_RADIO=user FAKE_PICK="i:$A" FAKE_FORMATS=VST3 \
            must "$setup"
        expect_receipt "$A" "$vA" "VST3"
        expect_only "$d_vst3/$nA.vst3" "$d_data/installed/$A" "$d_data/setup.sh" "$d_data/LICENSES.txt"
        case "$b" in
            zenity) for opt in "--list --radiolist" "--list --checklist" "--hide-column=5 --print-column=5" "--question" "--info"; do
                    grep -qF -- "$opt" "$tmp/fake.log" || fail "zenity was never called with $opt"
                done ;;
            kdialog) for opt in "--radiolist" "--separate-output --checklist" "--yesno" "--msgbox"; do
                    grep -qF -- "$opt" "$tmp/fake.log" || fail "kdialog was never called with $opt"
                done ;;
            whiptail) for opt in "--notags --radiolist" "--notags --separate-output --checklist" "--yesno" "--msgbox"; do
                    grep -qF -- "$opt" "$tmp/fake.log" || fail "whiptail was never called with $opt"
                done ;;
        esac
        PATH="$fake:$PATH" MYZEL_UI="$b" FAKE_LOG="$tmp/fake.log" FAKE_RADIO=user FAKE_PICK="r:user:$A" FAKE_FORMATS=VST3 \
            must "$setup"
        [ -z "$(written)" ] || fail "$b: removal left $(written | head -n 5)"
    done
    pass "zenity, kdialog and whiptail: install and remove through the dialogs"

    printf '#!/usr/bin/env bash\necho "Gtk-WARNING **: Failed to open display" >&2\nexit 1\n' > "$fake/zenity"
    must_not env PATH="$fake:$PATH" MYZEL_UI=zenity "$setup"
    log_has "could not run"
    [ -z "$(written)" ] || fail "a failed dialog wrote $(written | head -n 5)"
    pass "a dialog that cannot open the display says so instead of reporting a cancel"

    # Without MYZEL_SYSTEM_ROOT the system-wide part re-runs through pkexec (desktop) or sudo; the fakes
    # put the root back, so nothing reaches the real /usr.
    for b in pkexec sudo; do
        cat > "$fake/$b" <<'EOF'
#!/usr/bin/env bash
[ "$1" != "--" ] || shift
printf '%s\n' "$(basename "$0") $*" >> "$FAKE_LOG"
MYZEL_SYSTEM_ROOT="$FAKE_ROOT" exec "$@"
EOF
        chmod +x "$fake/$b"
        [ "$(PATH="$fake:$PATH" command -v "$b")" = "$fake/$b" ] || fail "the fake $b is not first on PATH"
    done
    use_scope system
    for b in pkexec sudo; do
        : > "$tmp/fake.log"
        if [ "$b" = pkexec ]; then display=:99; else display=""; fi
        must env -u MYZEL_SYSTEM_ROOT -u WAYLAND_DISPLAY DISPLAY="$display" PATH="$fake:$PATH" \
            FAKE_LOG="$tmp/fake.log" FAKE_ROOT="$root" "$setup" --plugins "$A" --formats vst3 --system --yes
        grep -qF -- "$b " "$tmp/fake.log" || fail "the system-wide part did not go through $b"
        grep -qF -- "--elevated --system --yes --downgrade --plugins $A --formats vst3" "$tmp/fake.log" \
            || fail "$b got unexpected arguments: $(cat "$tmp/fake.log")"
        expect_receipt "$A" "$vA" "VST3"
        # The unprivileged parent reads the real /usr, so removal is checked through MYZEL_SYSTEM_ROOT only.
        must "$setup" --remove "$A" --system --yes
        [ -z "$(written)" ] || fail "$b: left $(written | head -n 5)"
    done
    pass "system-wide part re-runs through pkexec in a desktop session, else sudo"

    # A dismissed password prompt (pkexec 126) changes nothing, also not a per-user removal ticked with it.
    # The checklist rows depend on the real /usr/local here, so this runs only where it holds no Myzel plugin.
    must env -u MYZEL_SYSTEM_ROOT "$setup" --list
    if grep -qE '^[^ ]+ +system-wide +[A-Z]' "$log"; then
        echo "skip  dismissed password prompt (Myzel Audio plugins installed system-wide on this machine)"
    else
        use_scope user
        must "$setup" --plugins "$A" --formats vst3 --yes
        before="$(snapshot "$A")"
        printf '#!/usr/bin/env bash\necho dismissed >> "$FAKE_LOG"\nexit 126\n' > "$fake/pkexec"
        : > "$tmp/fake.log"
        # Answers: system-wide, tick the per-user remove row, accept the checklist, all formats, confirm.
        must_not env -u MYZEL_SYSTEM_ROOT -u WAYLAND_DISPLAY DISPLAY=:99 PATH="$fake:$PATH" FAKE_LOG="$tmp/fake.log" \
            bash -c 'printf "2\n%s\n\n\ny\n" "$1" | "$2"' _ "$((${#ids[@]} + 1))" "$setup"
        grep -qx dismissed "$tmp/fake.log" || fail "the run never asked pkexec for the system-wide part"
        log_has "Cancelled; nothing changed."
        [ "$before" = "$(snapshot "$A")" ] || fail "a dismissed password prompt still removed the per-user $A"
        must "$setup" --remove-all --yes
        [ -z "$(written)" ] || fail "left $(written | head -n 5)"
        pass "a dismissed password prompt changes nothing in either scope"
    fi
    user_data_intact
}

echo "Testing $(basename "$run") against $stage ($A, $B, $C)"
test_scope user
test_scope system
test_extras
echo "All Linux installer tests passed."
