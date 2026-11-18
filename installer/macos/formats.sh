# The macOS formats and where they install (installer/PLAN.md, Install locations), for build.sh and test.sh.
# Sourced, never run. Bash 3.2. uninstall.sh carries its own copy of the table, since it ships alone.

MYZEL_MAC_FORMATS="AU VST3 CLAP Standalone"

myzel_mac_dir()
{
    case "$1" in
        AU) echo "/Library/Audio/Plug-Ins/Components" ;;
        VST3) echo "/Library/Audio/Plug-Ins/VST3" ;;
        CLAP) echo "/Library/Audio/Plug-Ins/CLAP" ;;
        Standalone) echo "/Applications/Myzel Audio" ;;
        *) return 1 ;;
    esac
}

myzel_mac_ext()
{
    case "$1" in
        AU) echo ".component" ;;
        VST3) echo ".vst3" ;;
        CLAP) echo ".clap" ;;
        Standalone) echo ".app" ;;
        *) return 1 ;;
    esac
}

myzel_mac_title()
{
    case "$1" in
        AU) echo "Audio Unit (AU)" ;;
        VST3) echo "VST3" ;;
        CLAP) echo "CLAP" ;;
        Standalone) echo "Standalone app" ;;
        *) return 1 ;;
    esac
}

# The format as it appears in pkg identifiers and choice ids: au, vst3, clap, standalone.
myzel_mac_key()
{
    printf '%s\n' "$1" | tr '[:upper:]' '[:lower:]'
}

# Formats of a staged plugin that the macOS installer ships, in MYZEL_MAC_FORMATS order.
myzel_mac_stage_formats()
{
    local format
    for format in $MYZEL_MAC_FORMATS; do
        [ -d "$1/$2/$format" ] && echo "$format"
    done
    return 0
}
