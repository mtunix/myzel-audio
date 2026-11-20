#!/usr/bin/env bash
# Build the Windows suite installer (Inno Setup 6) from a stage directory.
# Usage: installer/windows/build.sh <stage-dir> <out-dir>   (tools/installer.sh calls it)
#   Windows (Git Bash): ISCC from $ISCC, the Inno Setup 6 install folder or PATH.
#   Linux: ISCC under Wine; a pinned Inno Setup is installed into tools/.bin on first use.
set -euo pipefail

here="$(cd "$(dirname "$0")" && pwd)"
. "$here/../common.sh"

stage="${1:?usage: installer/windows/build.sh <stage-dir> <out-dir>}"
out="${2:?usage: installer/windows/build.sh <stage-dir> <out-dir>}"
stage="$(cd "$stage" && pwd)"
mkdir -p "$out"
out="$(cd "$out" && pwd)"

inno_version="6.7.4"
inno_sha256="a549929319f9120716677c685535ca4671e4b937b5fdc87e2585f32d8eb30e8c"
inno_url="https://github.com/jrsoftware/issrc/releases/download/is-${inno_version//./_}/innosetup-$inno_version.exe"

fail()
{
    echo "installer/windows/build.sh: $*" >&2
    exit 1
}

suite="$(myzel_suite_version)"
[[ "$suite" =~ ^[0-9]+(\.[0-9]+){0,3}$ ]] || fail "installer/VERSION '$suite' is not a version such as 1.2.3."

case "$(myzel_host_os)" in
    windows)
        winpath() { cygpath -w "$1"; }
        iscc="${ISCC:-}"
        # release.yml sets ISCC as a Windows path (backslashes).
        [ -z "$iscc" ] || iscc="$(cygpath -u "$iscc")"
        if [ -z "$iscc" ]; then
            for candidate in "C:/Program Files (x86)/Inno Setup 6/ISCC.exe" "C:/Program Files/Inno Setup 6/ISCC.exe"; do
                if [ -f "$candidate" ]; then
                    iscc="$candidate"
                    break
                fi
            done
        fi
        [ -n "$iscc" ] || iscc="$(command -v ISCC.exe || command -v iscc || true)"
        [ -n "$iscc" ] || fail "Inno Setup 6 not found. Install it (choco install innosetup) or set ISCC."
        # Git Bash would rewrite ISCC's /O, /D options as paths.
        run_iscc() { MSYS_NO_PATHCONV=1 MSYS2_ARG_CONV_EXCL='*' "$iscc" "$@"; }
        ;;
    linux)
        command -v wine > /dev/null || fail "wine is needed to build the Windows installer on Linux."
        # Inno's own setup creates its (hidden) wizard window even when very silent, so Wine needs an
        # X display; a private Xvfb keeps windows off the desktop. One X session for every wine call.
        if [ -z "${MYZEL_WINE_XVFB:-}" ] && command -v xvfb-run > /dev/null; then
            exec env MYZEL_WINE_XVFB=1 xvfb-run -a bash "$0" "$stage" "$out"
        fi
        [ -n "${DISPLAY:-}" ] || fail "Wine needs an X display: install xvfb-run (package xvfb) or set DISPLAY."
        export WINEPREFIX="$MYZEL_ROOT/tools/.bin/wine-innosetup-$inno_version"
        export WINEDEBUG="${WINEDEBUG:--all}"
        # No Mono or Gecko prompts while the prefix is created.
        export WINEDLLOVERRIDES="mscoree,mshtml="
        # A wineserver left from another display would keep its windowless desktop.
        wineserver -k 2> /dev/null || true
        iscc="$WINEPREFIX/drive_c/InnoSetup/ISCC.exe"
        if [ ! -f "$iscc" ]; then
            echo "Installing Inno Setup $inno_version into $WINEPREFIX..."
            download="$(mktemp -d)"
            trap 'rm -rf "$download"' EXIT
            curl -fsSL -o "$download/innosetup.exe" "$inno_url"
            if command -v sha256sum > /dev/null; then
                sum="$(sha256sum "$download/innosetup.exe" | cut -d' ' -f1)"
            else
                sum="$(shasum -a 256 "$download/innosetup.exe" | cut -d' ' -f1)"
            fi
            [ "$sum" = "$inno_sha256" ] || fail "Inno Setup download has SHA-256 $sum, expected $inno_sha256."
            mkdir -p "$WINEPREFIX"
            wine "$download/innosetup.exe" /VERYSILENT /SUPPRESSMSGBOXES /NORESTART /SP- /CURRENTUSER /NOICONS \
                '/DIR=C:\InnoSetup' "/LOG=$(winepath -w "$download/install.log" 2> /dev/null)" || true
            wineserver -w
            if [ ! -f "$iscc" ]; then
                tail -n 20 "$download/install.log" >&2 2> /dev/null || true
                fail "Inno Setup did not install into $WINEPREFIX."
            fi
        fi
        winpath() { winepath -w "$1" 2> /dev/null; }
        run_iscc() { wine "$iscc" "$@"; }
        ;;
    *) fail "build the Windows installer on Windows (Git Bash) or on Linux with Wine." ;;
esac

gen="$(mktemp -d)"
trap 'rm -rf "$gen" "${download:-}"' EXIT

# Inno string literal: quotes doubled, braces doubled (they start constants).
iss_text()
{
    local s="${1//\"/\"\"}"
    s="${s//\{/\{\{}"
    printf '%s' "$s"
}

pascal_text()
{
    printf '%s' "${1//\'/\'\'}"
}

defines=()
components=""
files=""
icons=""
cases=""
index=0
for id in $(myzel_plugin_ids); do
    [[ "$id" =~ ^[a-z][a-z0-9_]*$ ]] || fail "plugin id '$id' in installer/plugins.tsv is not lower-case letters and digits."
    name="$(myzel_plugin_name "$id")"
    blurb="$(myzel_plugin_blurb "$id")"
    [ -f "$stage/$id/version" ] || fail "$id is not staged in $stage (tools/stage.sh)."
    version="$(myzel_stage_version "$stage" "$id")"
    [[ "$version" =~ ^[0-9]+(\.[0-9]+){0,3}$ ]] || fail "$stage/$id/version '$version' is not a version such as 1.2.3."
    defines+=("/DVersion_$id=$version")

    formats=""
    for format in $(myzel_stage_formats "$stage" "$id"); do
        case "$format" in
            VST3)
                [ -f "$stage/$id/VST3/$name.vst3/Contents/x86_64-win/$name.vst3" ] \
                    || fail "$stage/$id/VST3/$name.vst3 has no Contents/x86_64-win/$name.vst3: not a Windows stage."
                files+="Source: \"{#StageDir}\\$id\\VST3\\$name.vst3\\*\"; DestDir: \"{commoncf64}\\VST3\\$name.vst3\"; Components: $id; Tasks: vst3; Flags: ignoreversion recursesubdirs createallsubdirs"$'\n'
                ;;
            CLAP)
                [ -f "$stage/$id/CLAP/$name.clap" ] || fail "$stage/$id/CLAP/$name.clap is missing: not a Windows stage."
                files+="Source: \"{#StageDir}\\$id\\CLAP\\$name.clap\"; DestDir: \"{commoncf64}\\CLAP\"; Components: $id; Tasks: clap; Flags: ignoreversion"$'\n'
                ;;
            Standalone)
                [ -f "$stage/$id/Standalone/$name.exe" ] || fail "$stage/$id/Standalone/$name.exe is missing: not a Windows stage."
                files+="Source: \"{#StageDir}\\$id\\Standalone\\$name.exe\"; DestDir: \"{app}\"; Components: $id; Tasks: standalone; Flags: ignoreversion"$'\n'
                icons+="Name: \"{autoprograms}\\{#Vendor}\\$name\"; Filename: \"{app}\\$name.exe\"; Components: $id; Tasks: standalone"$'\n'
                ;;
            *) fail "$stage/$id/$format is not a Windows format (VST3, CLAP, Standalone): not a Windows stage." ;;
        esac
        formats="${formats:+$formats,}$format"
    done
    [ -n "$formats" ] || fail "$stage/$id has no formats."

    components+="Name: \"$id\"; Description: \"$(iss_text "$name") {#Version_$id} - $(iss_text "$blurb")\"; Flags: disablenouninstallwarning"$'\n'
    cases+="    $index: begin Id := '$id'; Name := '$(pascal_text "$name")'; Version := '{#Version_$id}'; Formats := '$formats'; end;"$'\n'
    index=$((index + 1))
done
[ "$index" -gt 0 ] || fail "installer/plugins.tsv lists no plugins."

# UTF-8 with a BOM, so ISCC reads non-ASCII blurbs as UTF-8.
{
    printf '\xef\xbb\xbf'
    echo "; Generated by installer/windows/build.sh from installer/plugins.tsv and the stage."
    echo
    echo "[Components]"
    printf '%s' "$components"
    echo
    echo "[Files]"
    printf '%s' "$files"
    echo
    echo "[Icons]"
    printf '%s' "$icons"
    echo
    echo "[Code]"
    echo "const"
    echo "  PluginCount = $index;"
    echo
    echo "procedure GetPlugin(Index: Integer; var Id, Name, Version, Formats: String);"
    echo "begin"
    echo "  case Index of"
    printf '%s' "$cases"
    echo "  end;"
    echo "end;"
} > "$gen/plugins.iss"

myzel_write_licenses "$gen/LICENSES.txt" $(myzel_plugin_ids)

target="$out/MyzelAudio-$suite-windows-x64-setup.exe"
rm -f "$target"
run_iscc /Q "/O$(winpath "$out")" "/DSuiteVersion=$suite" "/DStageDir=$(winpath "$stage")" \
    "/DGenDir=$(winpath "$gen")" "/DRepoRoot=$(winpath "$MYZEL_ROOT")" "${defines[@]}" \
    "$(winpath "$here/MyzelAudio.iss")" \
    || fail "ISCC failed."
[ -f "$target" ] || fail "ISCC finished but $target is missing."
echo "$target"
