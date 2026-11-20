#!/usr/bin/env bash
# Myzel Audio installer for Linux: checklist, detection, both scopes, install, removal (installer/PLAN.md).
# Runs from the extracted .run payload, or as the copy kept beside the receipts (--list and removal only).
set -euo pipefail
umask 022

if [ "${BASH_VERSINFO[0]}" -lt 4 ] || { [ "${BASH_VERSINFO[0]}" -eq 4 ] && [ "${BASH_VERSINFO[1]}" -lt 4 ]; }; then
    echo "The Myzel Audio installer needs bash 4.4 or newer." >&2
    exit 1
fi

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
self="$here/$(basename "${BASH_SOURCE[0]}")"
data_name="myzel-audio"
all_formats="VST3 CLAP LV2 Standalone"

payload=""
suite=""
if [ -f "$here/plugins.list" ] && [ -d "$here/stage" ]; then
    payload="$here"
    suite="$(tr -d '[:space:]' < "$payload/suite")"
fi

ui=text
died=""
dialog_err=""

usage()
{
    cat <<EOF
Myzel Audio installer${suite:+ $suite} for Linux.

Usage: ./MyzelAudio-<version>-linux-x86_64.run [-- options]
       $data_name/setup.sh [options]     (the copy kept beside the receipts: --list and removal)

With no options it asks: scope, plugins to install or remove, formats, then a summary.

  --list                 Show what this installer carries and what is installed, in both scopes.
  --plugins a,b          Install or update these plugins (ids as --list shows them).
  --formats f,g          Only these formats: vst3, clap, lv2, standalone (default: all).
  --system               System-wide, for all users (/usr/local/lib/vst3, /usr/lib/clap,
                         /usr/local/lib/lv2, /usr/local/bin). Default: per-user (~/.vst3, ~/.clap,
                         ~/.lv2, ~/.local/bin). Asks for an administrator password (pkexec or sudo).
  --remove a,b           Remove these plugins, every format, from the chosen scope.
  --remove-all           Remove every Myzel Audio plugin from the chosen scope.
  --yes                  Don't ask. Without --plugins or --remove, installs what the checklist would tick.
  --downgrade            With --yes, also replace a newer installed version (otherwise skipped).
  --help                 This text.

Presets and settings in ~/.config/Myzel Audio are never touched.

Environment:
  MYZEL_UI=zenity|kdialog|whiptail|text   Force a dialog back-end.
  MYZEL_SYSTEM_ROOT=<dir>                 Testing aid: prefixes every system-wide path with <dir> and
                                          skips the administrator prompt.
EOF
}

say()
{
    printf '%s\n' "$*"
}

warn()
{
    printf '%s\n' "$*" >&2
}

is_gui()
{
    [ "$ui" = zenity ] || [ "$ui" = kdialog ]
}

die()
{
    warn "$*"
    if is_gui; then
        ui_error "$*"
    fi
    died=1
    exit 1
}

cancelled()
{
    warn "Cancelled; nothing changed."
    died=1
    exit 1
}

on_exit()
{
    local rc=$1
    if [ "$rc" -ne 0 ] && [ -z "$died" ] && is_gui; then
        ui_error "The installer stopped with an error (exit $rc). Run it from a terminal to see the details."
    fi
    if [ -n "$dialog_err" ]; then rm -f -- "$dialog_err"; fi
}
trap 'on_exit $?' EXIT

scope_dir()
{
    local root="${MYZEL_SYSTEM_ROOT:-}" xdg="${XDG_DATA_HOME:-}"
    case "$xdg" in
        /*) ;;
        *) xdg="$HOME/.local/share" ;;
    esac
    if [ "$1" = user ]; then
        case "$2" in
            VST3) echo "$HOME/.vst3" ;;
            CLAP) echo "$HOME/.clap" ;;
            LV2) echo "$HOME/.lv2" ;;
            Standalone) echo "$HOME/.local/bin" ;;
            apps) echo "$xdg/applications" ;;
            icons) echo "$xdg/icons/hicolor/scalable/apps" ;;
            data) echo "$xdg/$data_name" ;;
        esac
    else
        case "$2" in
            VST3) echo "$root/usr/local/lib/vst3" ;;
            # The CLAP spec names no /usr/local path.
            CLAP) echo "$root/usr/lib/clap" ;;
            LV2) echo "$root/usr/local/lib/lv2" ;;
            Standalone) echo "$root/usr/local/bin" ;;
            apps) echo "$root/usr/local/share/applications" ;;
            icons) echo "$root/usr/local/share/icons/hicolor/scalable/apps" ;;
            data) echo "$root/usr/local/share/$data_name" ;;
        esac
    fi
}

bundle_name()
{
    case "$1" in
        VST3) echo "$2.vst3" ;;
        CLAP) echo "$2.clap" ;;
        LV2) echo "$2.lv2" ;;
        Standalone) echo "$2" ;;
    esac
}

format_path()
{
    echo "$(scope_dir "$1" "$2")/$(bundle_name "$2" "$3")"
}

desktop_path()
{
    echo "$(scope_dir "$1" apps)/$data_name-$2.desktop"
}

icon_path()
{
    echo "$(scope_dir "$1" icons)/$data_name-$2.svg"
}

receipt_dir()
{
    echo "$(scope_dir "$1" data)/installed"
}

scope_label()
{
    if [ "$1" = user ]; then echo per-user; else echo system-wide; fi
}

other_scope()
{
    if [ "$1" = user ]; then echo system; else echo user; fi
}

tilde()
{
    case "$1" in
        "$HOME"/*) echo "~${1#"$HOME"}" ;;
        *) echo "$1" ;;
    esac
}

# Receipt paths are trusted only inside the scope's own install folders.
path_ok()
{
    local d k
    case "$2" in
        */../* | */.. | *$'\n'*) return 1 ;;
    esac
    for k in VST3 CLAP LV2 Standalone apps icons; do
        d="$(scope_dir "$1" "$k")"
        case "$2" in
            "$d"/?*) return 0 ;;
        esac
    done
    return 1
}

in_list()
{
    local needle="$1" x
    shift
    for x in "$@"; do
        [ "$x" != "$needle" ] || return 0
    done
    return 1
}

# Splits a comma or space separated value into the array csv without glob expansion.
split_csv()
{
    IFS=', ' read -r -a csv <<< "$1"
}

join_comma()
{
    local out="" x
    for x in "$@"; do
        out="${out:+$out, }$x"
    done
    echo "$out"
}

ver_cmp()
{
    if [ "$1" = "$2" ]; then
        echo eq
    elif [ "$(printf '%s\n%s\n' "$1" "$2" | sort -V | head -n 1)" = "$1" ]; then
        echo lt
    else
        echo gt
    fi
}

# What this payload carries, from plugins.list: id, name, version, formats, blurb.
declare -a ids=()
declare -A pname=() pver=() pfmts=() pblurb=()

load_catalog()
{
    local id name ver fmts blurb
    [ -n "$payload" ] || return 0
    while IFS=$'\t' read -r id name ver fmts blurb; do
        [ -n "$id" ] || continue
        ids+=("$id")
        pname[$id]="$name"
        pver[$id]="$ver"
        pfmts[$id]="${fmts//,/ }"
        pblurb[$id]="$blurb"
    done < "$payload/plugins.list"
    return 0
}

is_listed()
{
    [ -n "${pname[$1]:-}" ]
}

# Detection per "scope:id": receipt first, then moduleinfo.json in the VST3 bundle, else unknown version.
declare -A found=() fver=() ffmts=() fname=()
declare -A found_list=([user]="" [system]="")

receipt_get()
{
    awk -v k="$2" 'index($0, k "=") == 1 { print substr($0, length(k) + 2); exit }' "$1"
}

receipt_files()
{
    sed -n 's/^file=//p' "$1"
}

moduleinfo_version()
{
    local f="$1/Contents/Resources/moduleinfo.json"
    if [ -f "$f" ]; then
        sed -n '/"Version"/{s/.*"Version":[[:space:]]*"\([^"]*\)".*/\1/p;q;}' "$f"
    fi
    return 0
}

detect_scope()
{
    local s=$1 rdir r id name f fmts ver mi k
    local -a cand=()
    local -A seen=() scanned=() named=()
    rdir="$(receipt_dir "$s")"

    for id in "${ids[@]}"; do
        cand+=("$id")
        named[${pname[$id]}]=1
    done
    if [ -d "$rdir" ]; then
        for r in "$rdir"/*; do
            if [ -f "$r" ]; then
                id="$(basename "$r")"
                cand+=("$id")
                name="$(receipt_get "$r" name)"
                if [ -n "$name" ]; then named[$name]=1; fi
            fi
        done
    fi
    # Hand copies of plugins this installer doesn't carry: find them by vendor.
    for mi in "$(scope_dir "$s" VST3)"/*.vst3/Contents/Resources/moduleinfo.json; do
        [ -f "$mi" ] || continue
        grep -q '"Vendor":[[:space:]]*"Myzel Audio"' "$mi" || continue
        name="$(basename "${mi%/Contents/Resources/moduleinfo.json}" .vst3)"
        [ -z "${named[$name]:-}" ] || continue
        id="$(printf '%s' "$name" | tr '[:upper:]' '[:lower:]')"
        scanned[$id]="$name"
        cand+=("$id")
    done

    for id in "${cand[@]}"; do
        [[ $id =~ ^[a-z0-9][a-z0-9_-]*$ ]] || continue
        [ -z "${seen[$id]:-}" ] || continue
        seen[$id]=1
        r="$rdir/$id"
        name="${pname[$id]:-}"
        if [ -z "$name" ] && [ -f "$r" ]; then name="$(receipt_get "$r" name)"; fi
        if [ -z "$name" ]; then name="${scanned[$id]:-}"; fi
        [ -n "$name" ] || continue

        fmts=""
        for f in $all_formats; do
            if [ -e "$(format_path "$s" "$f" "$name")" ]; then fmts="$fmts $f"; fi
        done
        fmts="${fmts# }"
        [ -n "$fmts" ] || continue

        ver=""
        if [ -f "$r" ]; then
            ver="$(receipt_get "$r" version)"
        elif [ -d "$(format_path "$s" VST3 "$name")" ]; then
            ver="$(moduleinfo_version "$(format_path "$s" VST3 "$name")")"
        fi
        k="$s:$id"
        found[$k]=1
        fver[$k]="$ver"
        ffmts[$k]="$fmts"
        fname[$k]="$name"
        found_list[$s]="${found_list[$s]:+${found_list[$s]} }$id"
    done
    return 0
}

detect_all()
{
    found=()
    fver=()
    ffmts=()
    fname=()
    found_list=([user]="" [system]="")
    detect_scope user
    detect_scope system
}

# Sets st_kind (new, older, unknown, same, newer) and st_text for a plugin this installer carries.
install_status()
{
    local k="$1:$2" have carried="${pver[$2]}"
    if [ -z "${found[$k]:-}" ]; then
        st_kind=new
        st_text="not installed"
        return 0
    fi
    have="${fver[$k]}"
    if [ -z "$have" ]; then
        st_kind=unknown
        st_text="installed (unknown version) → replaces with $carried"
        return 0
    fi
    case "$(ver_cmp "$have" "$carried")" in
        lt) st_kind=older; st_text="installed $have → updates to $carried" ;;
        eq) st_kind=same; st_text="installed $have (up to date)" ;;
        gt) st_kind=newer; st_text="installed $have (newer)" ;;
    esac
    return 0
}

found_text()
{
    local k="$1:$2"
    if [ -n "${fver[$k]}" ]; then echo "installed ${fver[$k]}"; else echo "installed (unknown version)"; fi
}

formats_text()
{
    local f out=""
    for f in $1; do out="${out:+$out, }$f"; done
    echo "${out:--}"
}

pick_ui()
{
    dialog_err="$(mktemp "${TMPDIR:-/tmp}/myzel-audio-dialog.XXXXXX")"
    if [ -n "${MYZEL_UI:-}" ]; then
        case "$MYZEL_UI" in
            zenity | kdialog | whiptail | text) ui="$MYZEL_UI" ;;
            *) die "MYZEL_UI must be zenity, kdialog, whiptail or text." ;;
        esac
        return 0
    fi
    local display="${DISPLAY:-}${WAYLAND_DISPLAY:-}"
    if [ -n "$display" ] && command -v zenity > /dev/null 2>&1; then
        ui=zenity
    elif [ -n "$display" ] && command -v kdialog > /dev/null 2>&1; then
        ui=kdialog
    elif [ -t 0 ] && [ -t 1 ] && command -v whiptail > /dev/null 2>&1; then
        ui=whiptail
    else
        ui=text
    fi
}

# Cancel is exit 1 (whiptail: 255 for Esc), but zenity also exits 1 when it cannot open the display.
dialog_rc()
{
    local rc=$1
    if { [ "$rc" = 1 ] || { [ "$ui" = whiptail ] && [ "$rc" = 255 ]; }; } \
        && ! grep -qiE 'open display|connect to display' "$dialog_err" 2> /dev/null; then
        return 1
    fi
    die "The $ui dialog could not run (exit $rc). Run the installer in a terminal with MYZEL_UI=text to answer there."
}

term_size()
{
    local size
    size="$(stty size 2> /dev/null < /dev/tty || true)"
    term_lines="${size% *}"
    term_cols="${size#* }"
    [[ $term_lines =~ ^[0-9]+$ ]] || term_lines=24
    [[ $term_cols =~ ^[0-9]+$ ]] || term_cols=80
}

# whiptail draws on stdout and answers on stderr; swap them so $(...) captures the answer.
wt()
{
    whiptail "$@" 3>&1 1>&2 2>&3
}

# Sets wt_h, wt_w and wt_list for a box with this text and a list of $2 rows (0 for none).
wt_box_size()
{
    local lines rows="${2:-0}"
    term_size
    wt_w=$((term_cols - 4))
    [ "$wt_w" -le 100 ] || wt_w=100
    lines="$(printf '%s\n' "$1" | awk -v w=$((wt_w - 4)) '{ n += length($0) > w ? int((length($0) + w - 1) / w) : 1 } END { print n }')"
    wt_h=$((lines + 7 + rows))
    [ "$wt_h" -le $((term_lines - 1)) ] || wt_h=$((term_lines - 1))
    wt_list=$((wt_h - lines - 7))
    [ "$wt_list" -ge 1 ] || wt_list=1
}

# zenity's list dialog parses --text as Pango markup after unescaping backslashes.
zenity_markup()
{
    printf '%s' "$1" | sed -e 's/\\/\\\\/g' -e 's/&/\&amp;/g' -e 's/</\&lt;/g' -e 's/>/\&gt;/g'
}

ui_error()
{
    case "$ui" in
        zenity) zenity --error --no-markup --title "Myzel Audio" --width 520 --text "$1" 2> /dev/null || true ;;
        kdialog) kdialog --title "Myzel Audio" --error "$1" 2> /dev/null || true ;;
        *) ;;
    esac
}

ui_info()
{
    case "$ui" in
        zenity) zenity --info --no-markup --title "$1" --width 560 --text "$2" 2> /dev/null || true ;;
        kdialog) kdialog --title "$1" --msgbox "$2" 2> /dev/null || true ;;
        whiptail) wt_box_size "$2"; wt --title "$1" --msgbox "$2" "$wt_h" "$wt_w" || true ;;
        text) printf '\n%s\n' "$2" ;;
    esac
}

# ui_confirm title text ok-label: succeeds when the user agrees.
ui_confirm()
{
    local answer
    case "$ui" in
        zenity) zenity --question --no-markup --title "$1" --width 560 --text "$2" --ok-label "$3" --cancel-label Cancel 2> "$dialog_err" || dialog_rc $? ;;
        kdialog) kdialog --title "$1" --yes-label "$3" --no-label Cancel --yesno "$2" 2> "$dialog_err" || dialog_rc $? ;;
        whiptail) wt_box_size "$2"; wt --title "$1" --yes-button "$3" --no-button Cancel --yesno "$2" "$wt_h" "$wt_w" || dialog_rc $? ;;
        text)
            printf '\n%s\n%s? [y/N] ' "$2" "$3" >&2
            if ! IFS= read -r answer; then answer=""; fi
            [[ $answer == [yY]* ]]
            ;;
    esac
}

# ui_radio title text key label [key label ...]: prints the chosen key; the first pair is the default.
ui_radio()
{
    local title="$1" text="$2" out i n answer
    shift 2
    local -a keys=() labels=() args=()
    while [ $# -gt 0 ]; do
        keys+=("$1")
        labels+=("$2")
        shift 2
    done
    n=${#keys[@]}
    case "$ui" in
        zenity)
            for ((i = 0; i < n; i++)); do
                if [ "$i" = 0 ]; then args+=(TRUE); else args+=(FALSE); fi
                args+=("${labels[i]}" "${keys[i]}")
            done
            out="$(zenity --list --radiolist --title "$title" --text "$(zenity_markup "$text")" --width 680 --height 420 \
                --column "" --column "Choice" --column "key" --hide-column=3 --print-column=3 \
                "${args[@]}" 2> "$dialog_err")" || dialog_rc $? || return 1
            ;;
        kdialog)
            for ((i = 0; i < n; i++)); do
                args+=("${keys[i]}" "${labels[i]}")
                if [ "$i" = 0 ]; then args+=(on); else args+=(off); fi
            done
            out="$(kdialog --title "$title" --radiolist "$text" "${args[@]}" 2> "$dialog_err")" || dialog_rc $? || return 1
            ;;
        whiptail)
            for ((i = 0; i < n; i++)); do
                args+=("${keys[i]}" "${labels[i]}")
                if [ "$i" = 0 ]; then args+=(ON); else args+=(OFF); fi
            done
            wt_box_size "$text" "$n"
            out="$(wt --title "$title" --notags --radiolist "$text" "$wt_h" "$wt_w" "$wt_list" "${args[@]}")" || dialog_rc $? || return 1
            ;;
        text)
            printf '\n%s\n\n' "$text" >&2
            for ((i = 0; i < n; i++)); do
                printf '  %d) %s\n' $((i + 1)) "${labels[i]}" >&2
            done
            while :; do
                printf '\nChoose 1-%d [1], q to cancel: ' "$n" >&2
                if ! IFS= read -r answer; then answer=""; fi
                case "$answer" in
                    q | Q) return 1 ;;
                    "") out="${keys[0]}"; break ;;
                esac
                if [[ $answer =~ ^[0-9]+$ ]] && [ "$answer" -ge 1 ] && [ "$answer" -le "$n" ]; then
                    out="${keys[answer - 1]}"
                    break
                fi
                printf '"%s" is not a choice.\n' "$answer" >&2
            done
            ;;
    esac
    out="$(printf '%s' "$out" | tr -d '"\r\n')"
    [ -n "$out" ] || return 1
    printf '%s\n' "$out"
}

# One-line row for kdialog and whiptail: the group's first word only when the list mixes groups.
row_text()
{
    if [ -n "$single_group" ]; then
        printf '%s — %s' "$2" "$3"
    else
        printf '%s %s — %s' "${1%% *}" "$2" "$3"
    fi
}

# ui_checklist title text key on|off group label detail [...]: prints the ticked keys, one per line.
ui_checklist()
{
    local title="$1" text="$2" out i n line tok last mark single_group
    local -a csv=()
    shift 2
    local -a keys=() states=() groups=() labels=() details=() args=()
    while [ $# -gt 0 ]; do
        keys+=("$1")
        states+=("$2")
        groups+=("$3")
        labels+=("$4")
        details+=("$5")
        shift 5
    done
    n=${#keys[@]}
    single_group=1
    for ((i = 1; i < n; i++)); do
        if [ "${groups[i]}" != "${groups[0]}" ]; then single_group=""; fi
    done
    case "$ui" in
        zenity)
            for ((i = 0; i < n; i++)); do
                if [ "${states[i]}" = on ]; then args+=(TRUE); else args+=(FALSE); fi
                args+=("${groups[i]}" "${labels[i]}" "${details[i]}" "${keys[i]}")
            done
            out="$(zenity --list --checklist --title "$title" --text "$(zenity_markup "$text")" --width 980 --height 520 \
                --separator "|" --column "" --column "Action" --column "Plugin" --column "On this computer" --column "key" \
                --hide-column=5 --print-column=5 "${args[@]}" 2> "$dialog_err")" || dialog_rc $? || return 1
            printf '%s\n' "$out" | tr '|' '\n'
            ;;
        kdialog)
            for ((i = 0; i < n; i++)); do
                args+=("${keys[i]}" "$(row_text "${groups[i]}" "${labels[i]}" "${details[i]}")" "${states[i]}")
            done
            kdialog --title "$title" --separate-output --checklist "$text" "${args[@]}" 2> "$dialog_err" || dialog_rc $? || return 1
            ;;
        whiptail)
            for ((i = 0; i < n; i++)); do
                if [ "${states[i]}" = on ]; then mark=ON; else mark=OFF; fi
                # A terminal row has no room for the blurb.
                args+=("${keys[i]}" "$(row_text "${groups[i]}" "${labels[i]%%: *}" "${details[i]}")" "$mark")
            done
            wt_box_size "$text" "$n"
            wt --title "$title" --notags --separate-output --checklist "$text" "$wt_h" "$wt_w" "$wt_list" "${args[@]}" || dialog_rc $? || return 1
            ;;
        text)
            printf '\n%s\n' "$text" >&2
            while :; do
                last=""
                for ((i = 0; i < n; i++)); do
                    if [ "${groups[i]}" != "$last" ]; then
                        printf '\n  %s\n' "${groups[i]}" >&2
                        last="${groups[i]}"
                    fi
                    if [ "${states[i]}" = on ]; then mark=x; else mark=" "; fi
                    printf '  [%s] %2d  %s\n           %s\n' "$mark" $((i + 1)) "${labels[i]}" "${details[i]}" >&2
                done
                printf '\nType numbers to tick or untick (e.g. "1 3"), Enter to continue, q to cancel: ' >&2
                # End of input accepts the list as shown.
                if ! IFS= read -r line; then line=""; fi
                case "$line" in
                    q | Q) return 1 ;;
                    "") break ;;
                esac
                split_csv "$line"
                for tok in "${csv[@]}"; do
                    if [[ $tok =~ ^[0-9]+$ ]] && [ "$tok" -ge 1 ] && [ "$tok" -le "$n" ]; then
                        if [ "${states[tok - 1]}" = on ]; then states[tok - 1]=off; else states[tok - 1]=on; fi
                    else
                        printf '  "%s" is not a row number.\n' "$tok" >&2
                    fi
                done
            done
            for ((i = 0; i < n; i++)); do
                if [ "${states[i]}" = on ]; then printf '%s\n' "${keys[i]}"; fi
            done
            ;;
    esac
    return 0
}

write_desktop()
{
    local s=$1 id=$2 name=$3 exec_path=$4 desktop icon tmp quoted
    desktop="$(desktop_path "$s" "$id")"
    icon="$(icon_path "$s" "$id")"
    mkdir -p "$(dirname "$desktop")" "$(dirname "$icon")"

    if [ -f "$payload/stage/$id/icon.svg" ]; then
        cp -- "$payload/stage/$id/icon.svg" "$icon"
    else
        cat > "$icon" <<EOF
<svg xmlns="http://www.w3.org/2000/svg" width="256" height="256" viewBox="0 0 256 256">
  <rect x="16" y="16" width="224" height="224" rx="48" fill="#2e3138"/>
  <text x="128" y="172" font-family="sans-serif" font-size="136" font-weight="bold" text-anchor="middle" fill="#f2f2f2">${name:0:1}</text>
</svg>
EOF
    fi
    chmod 644 "$icon"

    # Desktop Entry spec: quote Exec, escape " ` $ \ inside it, escape backslashes again for the string, double %.
    quoted="$(printf '%s' "$exec_path" | sed -e 's/[\\"`$]/\\&/g' -e 's/\\/\\\\/g' -e 's/%/%%/g')"
    tmp="$desktop.tmp.$$"
    cat > "$tmp" <<EOF
[Desktop Entry]
Type=Application
Name=$name
GenericName=Audio plugin
Comment=${pblurb[$id]}
Exec="$quoted"
Icon=$icon
Terminal=false
Categories=AudioVideo;Audio;
EOF
    chmod 644 "$tmp"
    mv -f -- "$tmp" "$desktop"
}

write_receipt()
{
    local s=$1 id=$2 name=$3 ver=$4 r tmp f fmts=""
    shift 4
    r="$(receipt_dir "$s")/$id"
    mkdir -p "$(dirname "$r")"
    for f in $all_formats; do
        if [ -e "$(format_path "$s" "$f" "$name")" ]; then fmts="$fmts $f"; fi
    done
    tmp="$r.tmp.$$"
    {
        printf 'id=%s\nname=%s\nversion=%s\nsuite=%s\nformats=%s\n' "$id" "$name" "$ver" "$suite" "${fmts# }"
        for f in "$@"; do
            printf 'file=%s\n' "$f"
        done
    } > "$tmp"
    chmod 644 "$tmp"
    mv -f -- "$tmp" "$r"
}

# The kept copy removes and lists without the payload; replaced atomically so a running copy keeps its inode.
keep_self()
{
    local data tmp
    data="$(scope_dir "$1" data)"
    [ "$self" != "$data/setup.sh" ] || return 0
    mkdir -p "$data"
    tmp="$data/.setup.sh.$$"
    cp -- "$self" "$tmp"
    chmod 755 "$tmp"
    mv -f -- "$tmp" "$data/setup.sh"
    if [ -f "$here/LICENSES.txt" ]; then
        cp -- "$here/LICENSES.txt" "$data/LICENSES.txt"
        chmod 644 "$data/LICENSES.txt"
    fi
}

install_plugin()
{
    local s=$1 id=$2 name="${pname[$2]}" ver="${pver[$2]}" f src dest r old used=""
    local -a files=()
    [ -n "$name" ] || die "Internal error: no name for '$id'."
    r="$(receipt_dir "$s")/$id"
    for f in $sel_formats; do
        [[ " ${pfmts[$id]} " == *" $f "* ]] || continue
        src="$payload/stage/$id/$f/$(bundle_name "$f" "$name")"
        dest="$(format_path "$s" "$f" "$name")"
        [ -e "$src" ] || die "The installer is damaged: $src is missing. Download it again."
        mkdir -p "$(dirname "$dest")"
        # Remove first, so a host that has the old binary loaded keeps its mapped copy instead of crashing.
        rm -rf -- "$dest"
        cp -R -- "$src" "$dest"
        chmod -R u+rwX,go+rX,go-w -- "$dest"
        files+=("$dest")
        used="${used:+$used, }$f"
        if [ "$f" = Standalone ]; then
            chmod 755 -- "$dest"
            write_desktop "$s" "$id" "$name" "$dest"
            files+=("$(desktop_path "$s" "$id")" "$(icon_path "$s" "$id")")
        fi
    done
    if [ -z "$used" ]; then
        say "$name: this installer has none of the chosen formats for it; skipped."
        return 0
    fi
    # Formats installed earlier and not chosen now stay; the receipt keeps tracking them.
    if [ -f "$r" ]; then
        while IFS= read -r old; do
            if ! in_list "$old" "${files[@]}" && path_ok "$s" "$old" && [ -e "$old" ]; then files+=("$old"); fi
        done < <(receipt_files "$r")
    fi
    write_receipt "$s" "$id" "$name" "$ver" "${files[@]}"
    keep_self "$s"
    say "Installed $name $ver ($(scope_label "$s"): $used)"
}

remove_plugin()
{
    local s=$1 id=$2 k="$1:$2" name r p f
    local -a paths=()
    name="${fname[$k]:-${pname[$id]:-}}"
    r="$(receipt_dir "$s")/$id"
    if [ -f "$r" ]; then
        while IFS= read -r p; do paths+=("$p"); done < <(receipt_files "$r")
        if [ -z "$name" ]; then name="$(receipt_get "$r" name)"; fi
    fi
    if [ -n "$name" ]; then
        for f in $all_formats; do paths+=("$(format_path "$s" "$f" "$name")"); done
    fi
    paths+=("$(desktop_path "$s" "$id")" "$(icon_path "$s" "$id")")
    for p in "${paths[@]}"; do
        if path_ok "$s" "$p" && { [ -e "$p" ] || [ -L "$p" ]; }; then rm -rf -- "$p"; fi
    done
    rm -f -- "$r"
    say "Removed ${name:-$id} ($(scope_label "$s"))"
}

# Drops receipts whose files are all gone; with none left, the kept copy and the data folder go too.
finalize_scope()
{
    local s=$1 data rdir r p ok any=0
    data="$(scope_dir "$s" data)"
    rdir="$data/installed"
    if [ -d "$rdir" ]; then
        for r in "$rdir"/*; do
            [ -f "$r" ] || continue
            ok=0
            while IFS= read -r p; do
                if [ -e "$p" ]; then ok=1; fi
            done < <(receipt_files "$r")
            if [ "$ok" = 1 ]; then any=1; else rm -f -- "$r"; fi
        done
    fi
    if [ "$any" = 0 ] && [ -d "$data" ]; then
        rm -f -- "$data/setup.sh" "$data/LICENSES.txt"
        rmdir -- "$rdir" 2> /dev/null || true
        rmdir -- "$data" 2> /dev/null || true
    fi
    return 0
}

run_scope()
{
    local s=$1 removes=$2 installs=$3 id
    for id in $removes; do remove_plugin "$s" "$id"; done
    for id in $installs; do install_plugin "$s" "$id"; done
    finalize_scope "$s"
}

needs_root()
{
    [ -z "${MYZEL_SYSTEM_ROOT:-}" ] && [ "$(id -u)" != 0 ]
}

elevate()
{
    local bash_path rc
    bash_path="$(command -v bash)"
    if [ -n "${DISPLAY:-}${WAYLAND_DISPLAY:-}" ] && command -v pkexec > /dev/null 2>&1; then
        say "Asking for an administrator password (pkexec) for the system-wide part..."
        rc=0
        pkexec "$bash_path" "$self" "$@" || rc=$?
        # pkexec: 126 is a dismissed dialog, 127 no authorisation (e.g. no polkit agent).
        case "$rc" in
            0) return 0 ;;
            126) cancelled ;;
            127) [ -t 0 ] || die "pkexec could not get administrator rights; run the installer from a terminal to use sudo." ;;
            *) die "The system-wide part failed (exit $rc)." ;;
        esac
    fi
    command -v sudo > /dev/null 2>&1 \
        || die "System-wide installs need administrator rights: run this as root, or install pkexec or sudo."
    say "Asking for an administrator password (sudo) for the system-wide part..."
    rc=0
    sudo -- "$bash_path" "$self" "$@" || rc=$?
    [ "$rc" = 0 ] || die "The system-wide part failed (exit $rc)."
}

# The plan: sel_install (ids, into $scope), sel_remove (scope:id), sel_formats.
scope=user
sel_install=""
sel_remove=""
sel_formats="$all_formats"

drop_removed_installs()
{
    local id kept=""
    for id in $sel_install; do
        # Ticking remove wins over install in the same scope.
        if [[ " $sel_remove " == *" $scope:$id "* ]]; then
            say "${pname[$id]}: removing, so not installing."
            continue
        fi
        kept="${kept:+$kept }$id"
    done
    sel_install="$kept"
}

# With --yes a newer installed version is skipped unless --downgrade; interactively it asks.
confirm_downgrades()
{
    local id kept=""
    for id in $sel_install; do
        install_status "$scope" "$id"
        if [ "$st_kind" = newer ] && [ -z "$opt_downgrade" ]; then
            if [ -n "$opt_yes" ]; then
                say "${pname[$id]}: ${fver[$scope:$id]} is installed, newer than ${pver[$id]}; skipped (--downgrade replaces it)."
                continue
            fi
            if ! ui_confirm "Myzel Audio" "${pname[$id]} ${fver[$scope:$id]} is installed ($(scope_label "$scope")), newer than ${pver[$id]} in this installer. Replace it with ${pver[$id]}?" "Downgrade"; then
                continue
            fi
        fi
        kept="${kept:+$kept }$id"
    done
    sel_install="$kept"
}

summary_text()
{
    local id s word="" item fmts=""
    for item in $sel_formats; do fmts="${fmts:+$fmts, }$item"; done
    if [ -n "$sel_install" ]; then
        if [ "$scope" = system ] && [ -n "${MYZEL_SYSTEM_ROOT:-}" ]; then
            printf 'Into: system-wide (under %s)\n' "$MYZEL_SYSTEM_ROOT"
        else
            printf 'Into: %s\n' "$(scope_label "$scope")"
        fi
        for id in $sel_install; do
            install_status "$scope" "$id"
            case "$st_kind" in
                new) word="Install" ;;
                older | unknown) word="Update" ;;
                same) word="Reinstall" ;;
                newer) word="Downgrade" ;;
            esac
            printf '  %s %s %s\n' "$word" "${pname[$id]}" "${pver[$id]}"
        done
        printf 'Formats: %s\n' "$fmts"
    fi
    if [ -n "$sel_remove" ]; then
        printf 'Remove (every format):\n'
        for item in $sel_remove; do
            s="${item%%:*}"
            id="${item#*:}"
            printf '  %s (%s)\n' "${fname[$item]:-$id}" "$(scope_label "$s")"
        done
    fi
}

done_text()
{
    local f places=""
    if [ -n "$sel_install" ]; then
        for f in $sel_formats; do
            places="$places$(printf '\n  %-11s %s' "$f" "$(tilde "$(scope_dir "$scope" "$f")")")"
        done
        printf 'Done. The plugins are in:%s\n\nRescan plugins in your host.\n' "$places"
    else
        printf 'Done. Rescan plugins in your host.\n'
    fi
    if [ -f "$(scope_dir "$scope" data)/setup.sh" ]; then
        printf 'To remove plugins later: %s --remove <plugin>%s\n' "$(tilde "$(scope_dir "$scope" data)/setup.sh")" \
            "$([ "$scope" = system ] && echo " --system")"
    fi
}

apply_plan()
{
    local item s id user_rm="" sys_rm="" user_in="" sys_in="" fmt_arg="" f
    local -a args=()
    for item in $sel_remove; do
        s="${item%%:*}"
        id="${item#*:}"
        if [ "$s" = user ]; then user_rm="${user_rm:+$user_rm }$id"; else sys_rm="${sys_rm:+$sys_rm }$id"; fi
    done
    if [ "$scope" = user ]; then user_in="$sel_install"; else sys_in="$sel_install"; fi

    # System-wide first: a dismissed password prompt then leaves the per-user part untouched too.
    if [ -n "$sys_rm$sys_in" ]; then
        if needs_root && [ -z "$opt_elevated" ]; then
            for f in $sel_formats; do fmt_arg="${fmt_arg:+$fmt_arg,}$(printf '%s' "$f" | tr '[:upper:]' '[:lower:]')"; done
            args=(--elevated --system --yes --downgrade)
            if [ -n "$sys_in" ]; then args+=(--plugins "${sys_in// /,}" --formats "$fmt_arg"); fi
            if [ -n "$sys_rm" ]; then args+=(--remove "${sys_rm// /,}"); fi
            elevate "${args[@]}"
        else
            run_scope system "$sys_rm" "$sys_in"
        fi
    fi
    if [ -n "$user_rm$user_in" ]; then run_scope user "$user_rm" "$user_in"; fi
}

welcome_text()
{
    local id names=""
    for id in "${ids[@]}"; do names="${names:+$names, }${pname[$id]}"; done
    printf 'Myzel Audio %s\n\nPlugins in this installer: %s.\n\nFree software under the GNU Affero General Public License, version 3 (AGPLv3), with no warranty. Source: github.com/mtunix/myzel-audio' "$suite" "$names"
}

choose_scope()
{
    local system_label="For everyone on this computer (system-wide: /usr/local/lib/vst3, /usr/lib/clap, /usr/local/lib/lv2, /usr/local/bin; asks for an administrator password)"
    if [ -n "${MYZEL_SYSTEM_ROOT:-}" ]; then system_label="For everyone on this computer (system-wide, under $MYZEL_SYSTEM_ROOT)"; fi
    if [ -n "$opt_system" ]; then
        ui_confirm "Myzel Audio $suite" "$(welcome_text)

Installs system-wide, for everyone on this computer." "Continue" || cancelled
        scope=system
        return 0
    fi
    scope="$(ui_radio "Myzel Audio $suite" "$(welcome_text)

Where should the plugins go?" \
        user "Just for me (per-user: ~/.vst3, ~/.clap, ~/.lv2, ~/.local/bin)" \
        system "$system_label")" || cancelled
}

interactive()
{
    local id s k state label detail key other title text out
    local -a items=()

    if [ -n "$payload" ]; then
        choose_scope
    elif [ -n "$opt_system" ]; then
        scope=system
    fi
    detect_all
    other="$(other_scope "$scope")"

    for id in "${ids[@]}"; do
        install_status "$scope" "$id"
        case "$st_kind" in
            new | older | unknown) state=on ;;
            *) state=off ;;
        esac
        detail="$st_text"
        if [ -n "${found[$scope:$id]:-}" ]; then detail="$detail · $(formats_text "${ffmts[$scope:$id]}")"; fi
        if [ -n "${found[$other:$id]:-}" ]; then
            detail="$detail · also installed $(scope_label "$other"): a host may load either copy"
        fi
        items+=("i:$id" "$state" "Install or update" "${pname[$id]} ${pver[$id]}: ${pblurb[$id]}" "$detail")
    done
    for s in "$scope" "$other"; do
        for id in ${found_list[$s]}; do
            k="$s:$id"
            label="${fname[$k]} $(found_text "$s" "$id" | sed 's/^installed //')"
            detail="$(scope_label "$s"): $(formats_text "${ffmts[$k]}")"
            if [ -n "${found[$(other_scope "$s"):$id]:-}" ]; then detail="$detail · also installed $(scope_label "$(other_scope "$s")")"; fi
            items+=("r:$k" off "Remove" "$label" "$detail")
        done
    done

    if [ ${#items[@]} -eq 0 ]; then
        ui_info "Myzel Audio" "No Myzel Audio plugins are installed."
        exit 0
    fi

    title="Myzel Audio${suite:+ $suite}: plugins"
    if [ -n "$payload" ]; then
        text="Tick what to install or update ($(scope_label "$scope")) and what to remove. Removing wins over installing the same plugin."
    else
        text="Tick the plugins to remove (every format)."
    fi
    out="$(ui_checklist "$title" "$text" "${items[@]}")" || cancelled
    for key in $out; do
        case "$key" in
            i:*) sel_install="${sel_install:+$sel_install }${key#i:}" ;;
            r:*) sel_remove="${sel_remove:+$sel_remove }${key#r:}" ;;
        esac
    done
    drop_removed_installs

    if [ -n "$sel_install" ] && [ -z "$opt_formats" ]; then
        choose_formats
    fi
    confirm_downgrades

    if [ -z "$sel_install$sel_remove" ]; then
        ui_info "Myzel Audio" "Nothing ticked; nothing changed."
        exit 0
    fi
    ui_confirm "Myzel Audio: summary" "$(summary_text)" "$([ -n "$sel_install" ] && echo Install || echo Remove)" || cancelled
    apply_plan
    ui_info "Myzel Audio" "$(done_text)"
}

choose_formats()
{
    local f id offered="" chosen="" key out
    local -a items=()
    for f in $all_formats; do
        for id in $sel_install; do
            if [[ " ${pfmts[$id]} " == *" $f "* ]]; then
                offered="$offered $f"
                break
            fi
        done
    done
    for f in $offered; do
        case "$f" in
            VST3) items+=(VST3 on "Formats" "VST3" "most hosts") ;;
            CLAP) items+=(CLAP on "Formats" "CLAP" "Bitwig, Reaper and other CLAP hosts") ;;
            LV2) items+=(LV2 on "Formats" "LV2" "Ardour, Carla and other LV2 hosts") ;;
            Standalone) items+=(Standalone on "Formats" "Standalone" "the app on its own, with a menu entry") ;;
        esac
    done
    out="$(ui_checklist "Myzel Audio: formats" "Which formats should be installed? They apply to every plugin you ticked." "${items[@]}")" || cancelled
    for key in $out; do
        case " $all_formats " in
            *" $key "*) chosen="${chosen:+$chosen }$key" ;;
        esac
    done
    [ -n "$chosen" ] || die "No format ticked; nothing changed."
    sel_formats="$chosen"
}

non_interactive()
{
    local id
    local -a csv=()
    if [ -n "$opt_system" ]; then scope=system; fi
    detect_all

    if [ -n "$opt_plugins" ]; then
        split_csv "$opt_plugins"
        for id in "${csv[@]}"; do
            is_listed "$id" || die "'$id' is not in this installer (it has: $(join_comma "${ids[@]}"))."
            sel_install="${sel_install:+$sel_install }$id"
        done
    elif [ -n "$opt_yes" ] && [ -z "$opt_remove$opt_remove_all" ]; then
        for id in "${ids[@]}"; do
            install_status "$scope" "$id"
            case "$st_kind" in
                new | older | unknown) sel_install="${sel_install:+$sel_install }$id" ;;
            esac
        done
    fi

    if [ -n "$opt_remove_all" ]; then
        for id in ${found_list[$scope]}; do sel_remove="${sel_remove:+$sel_remove }$scope:$id"; done
    fi
    split_csv "$opt_remove"
    for id in "${csv[@]}"; do
        if [ -n "${found[$scope:$id]:-}" ]; then
            if [[ " $sel_remove " != *" $scope:$id "* ]]; then sel_remove="${sel_remove:+$sel_remove }$scope:$id"; fi
        else
            say "$id is not installed $(scope_label "$scope"); nothing to remove."
        fi
    done

    drop_removed_installs
    confirm_downgrades
    if [ -z "$sel_install$sel_remove" ]; then
        say "Nothing to do."
        return 0
    fi
    if [ -z "$opt_yes" ]; then
        ui_confirm "Myzel Audio: summary" "$(summary_text)" "$([ -n "$sel_install" ] && echo Install || echo Remove)" || cancelled
    fi
    apply_plan
    if [ -z "$opt_elevated" ]; then
        say ""
        done_text
    fi
}

list_status()
{
    local id s k st flagged=""
    local -a unlisted=()
    detect_all
    for s in user system; do
        for id in ${found_list[$s]}; do
            if ! is_listed "$id" && ! in_list "$id" "${unlisted[@]}"; then unlisted+=("$id"); fi
        done
    done
    if [ -n "$payload" ]; then
        say "Myzel Audio $suite: what this installer carries and what is installed"
    else
        say "Myzel Audio plugins on this computer"
    fi
    printf '%-12s %-12s %-28s %s\n' "Plugin" "Scope" "Formats" "Status"
    for id in "${ids[@]}" "${unlisted[@]}"; do
        for s in user system; do
            k="$s:$id"
            if is_listed "$id"; then
                install_status "$s" "$id"
                st="$st_text"
            elif [ -n "${found[$k]:-}" ]; then
                st="$(found_text "$s" "$id")${payload:+ (not in this installer)}"
            else
                st="not installed"
            fi
            printf '%-12s %-12s %-28s %s\n' "${pname[$id]:-${fname[$k]:-${fname[$(other_scope "$s"):$id]:-$id}}}" \
                "$(scope_label "$s")" "$(formats_text "${ffmts[$k]:-}")" "$st"
        done
        if [ -n "${found[user:$id]:-}" ] && [ -n "${found[system:$id]:-}" ]; then
            flagged="$flagged ${pname[$id]:-${fname[user:$id]}}"
        fi
    done
    for id in $flagged; do
        say "Note: $id is installed in both scopes (per-user and system-wide); a host may load either copy."
    done
    if [ -n "$payload" ]; then
        say "Plugin ids for --plugins and --remove: $(join_comma "${ids[@]}" "${unlisted[@]}")"
    fi
}

check_glibc()
{
    local need have
    [ -f "$payload/glibc" ] || return 0
    need="$(tr -d '[:space:]' < "$payload/glibc")"
    [ -n "$need" ] || return 0
    have="$(getconf GNU_LIBC_VERSION 2> /dev/null | awk '{ print $2 }' || true)"
    if [ -z "$have" ]; then
        die "The Myzel Audio plugins need GNU libc (glibc) $need or newer; this system's C library is not glibc."
    fi
    if [ "$(ver_cmp "$have" "$need")" = lt ]; then
        die "The Myzel Audio plugins need glibc $need or newer; this system has glibc $have. Update the distribution (for example Ubuntu 24.04 or newer) to use them."
    fi
}

opt_list=""
opt_plugins=""
opt_formats=""
opt_system=""
opt_yes=""
opt_remove=""
opt_remove_all=""
opt_downgrade=""
opt_elevated=""

need_value()
{
    [ $# -ge 2 ] && [ -n "$2" ] || die "$1 needs a value (see --help)."
}

while [ $# -gt 0 ]; do
    case "$1" in
        --list) opt_list=1 ;;
        --plugins) need_value "$@"; opt_plugins="$2"; shift ;;
        --plugins=*) opt_plugins="${1#*=}" ;;
        --formats) need_value "$@"; opt_formats="$2"; shift ;;
        --formats=*) opt_formats="${1#*=}" ;;
        --system) opt_system=1 ;;
        --yes | -y) opt_yes=1 ;;
        --remove) need_value "$@"; opt_remove="$2"; shift ;;
        --remove=*) opt_remove="${1#*=}" ;;
        --remove-all) opt_remove_all=1 ;;
        --downgrade) opt_downgrade=1 ;;
        --elevated) opt_elevated=1 ;;
        --help | -h) usage; exit 0 ;;
        *) die "Unknown option '$1' (see --help)." ;;
    esac
    shift
done

load_catalog

if [ -n "$opt_list" ]; then
    list_status
    exit 0
fi

if [ -n "$opt_formats" ]; then
    sel_formats=""
    split_csv "$opt_formats"
    for f in "${csv[@]}"; do
        case "$(printf '%s' "$f" | tr '[:upper:]' '[:lower:]')" in
            vst3) f=VST3 ;;
            clap) f=CLAP ;;
            lv2) f=LV2 ;;
            standalone) f=Standalone ;;
            *) die "Unknown format '$f' (expected vst3, clap, lv2 or standalone)." ;;
        esac
        sel_formats="${sel_formats:+$sel_formats }$f"
    done
fi

pick_ui

installing=1
if [ -n "$opt_remove$opt_remove_all" ] && [ -z "$opt_plugins" ]; then installing=""; fi
if [ -z "$payload" ]; then
    if [ -n "$opt_plugins" ]; then
        die "This copy of the installer only lists and removes plugins; run the downloaded MyzelAudio .run file to install."
    fi
    installing=""
fi
if [ -n "$installing" ]; then check_glibc; fi

if [ "$(id -u)" = 0 ] && [ -z "$opt_system" ] && [ -z "$opt_elevated" ]; then
    warn "Running as root: per-user means root's home ($HOME). Use --system to install for everyone."
fi

if [ -n "$opt_plugins$opt_remove$opt_remove_all$opt_yes" ]; then
    non_interactive
else
    interactive
fi
