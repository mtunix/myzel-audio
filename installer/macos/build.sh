#!/bin/bash
# Build the macOS suite installer: a flat product archive with one component pkg per plugin and format.
# Usage: installer/macos/build.sh <stage-dir> <out-dir>        (tools/installer.sh macos calls it)
#   Writes <out-dir>/MyzelAudio-<suite>-macos-universal.pkg, unsigned; every bundle in it is ad-hoc signed.
#   MYZEL_PKG_GENERATE_ONLY=1 writes only the generated parts (Distribution, resources, scripts,
#   uninstall.sh) to <out-dir>/macos-pkg and stops before signing and pkgbuild, on any OS.
# Runs under macOS /bin/bash 3.2.
set -euo pipefail

here="$(cd "$(dirname "$0")" && pwd)"
. "$here/../common.sh"
. "$here/formats.sh"

stage="${1:?usage: installer/macos/build.sh <stage-dir> <out-dir>}"
out="${2:?usage: installer/macos/build.sh <stage-dir> <out-dir>}"
generate_only="${MYZEL_PKG_GENERATE_ONLY:-0}"
suite="$(myzel_suite_version)"
product="MyzelAudio-$suite-macos-universal.pkg"
receipt_prefix="com.myzelaudio.pkg"
support_dir="/Library/Application Support/Myzel Audio"

die()
{
    echo "$*" >&2
    exit 1
}

if [ "$generate_only" != 1 ]; then
    [ "$(uname -s)" = Darwin ] || die "The macOS installer builds on macOS (MYZEL_PKG_GENERATE_ONLY=1 generates its parts anywhere)."
    for tool in pkgbuild productbuild codesign plutil ditto xattr file; do
        command -v "$tool" >/dev/null || die "$tool not found; it ships with macOS and the Xcode command line tools."
    done
fi

[ -d "$stage" ] || die "Stage directory $stage not found (tools/stage.sh fills it)."
mkdir -p "$out"
if [ "$generate_only" = 1 ]; then
    work="$out/macos-pkg"
    rm -rf "$work"
    mkdir -p "$work"
else
    work="$(mktemp -d "${TMPDIR:-/tmp}/myzel-pkg.XXXXXX")"
    trap 'rm -rf "$work"' EXIT
fi
frag="$work/fragments"
mkdir -p "$frag" "$work/resources" "$work/scripts" "$work/roots" "$work/plists" "$work/packages"

xml_escape()
{
    printf '%s' "$1" | sed -e 's/&/\&amp;/g' -e 's/</\&lt;/g' -e 's/>/\&gt;/g' -e 's/"/\&quot;/g' -e "s/'/\&#39;/g"
}

js_escape()
{
    printf '%s' "$1" | sed -e 's/\\/\\\\/g' -e "s/'/\\\\'/g"
}

sentence()
{
    case "$1" in
        *. | *! | *\?) printf '%s' "$1" ;;
        *) printf '%s.' "$1" ;;
    esac
}

# Copies a template, replacing each line that holds only @KEY@ with $frag/KEY and the inline
# @SUITE_VERSION@ and @FIRST_PLUGIN_ID@.
fill_template()
{
    awk -v dir="$frag" -v suite="$suite" -v first="$first_id" '
        /^[ \t]*@[A-Z_]+@[ \t]*$/ {
            key = $0
            gsub(/[ \t@]/, "", key)
            file = dir "/" key
            if ((getline line < file) <= 0) {
                print "fill_template: no fragment " key > "/dev/stderr"
                exit 1
            }
            print line
            while ((getline line < file) > 0)
                print line
            close(file)
            next
        }
        {
            gsub(/@SUITE_VERSION@/, suite)
            gsub(/@FIRST_PLUGIN_ID@/, first)
            print
        }
    ' "$1" > "$2"
}

ids="$(myzel_plugin_ids)"
[ -n "$ids" ] || die "installer/plugins.tsv lists no plugins."
first_id=""
for id in $ids; do
    case "$id" in
        '' | *[![:lower:][:digit:]-]*) die "Plugin id '$id' in installer/plugins.tsv: use lowercase letters, digits and '-' only." ;;
    esac
    [ -n "$first_id" ] || first_id="$id"
    [ -f "$stage/$id/version" ] || die "$id is in installer/plugins.tsv but not staged in $stage (tools/stage.sh)."
    if [ -z "$(myzel_mac_stage_formats "$stage" "$id")" ]; then
        die "$stage/$id holds no macOS format ($MYZEL_MAC_FORMATS). Stage it on macOS."
    fi
    for format in $(myzel_stage_formats "$stage" "$id"); do
        myzel_mac_dir "$format" >/dev/null || echo "Skipping $id $format: not a macOS format." >&2
    done
done

# Fragments for the Distribution, the resources and uninstall.sh.
: > "$frag/PLUGINS_JS"
: > "$frag/FORMATS_JS"
: > "$frag/PLUGIN_LIST_HTML"
: > "$frag/LOCATIONS_HTML"
: > "$frag/MYZEL_PLUGINS"
used_formats=""
install_outline=""
remove_outline=""
install_choices=""
remove_choices=""
pkg_refs="    <pkg-ref id=\"$receipt_prefix.support\" version=\"$suite\" onConclusion=\"none\">support.pkg</pkg-ref>"
nl='
'
separator=""

for id in $ids; do
    name="$(myzel_plugin_name "$id")"
    blurb="$(myzel_plugin_blurb "$id")"
    version="$(myzel_stage_version "$stage" "$id")"
    formats="$(myzel_mac_stage_formats "$stage" "$id")"
    xname="$(xml_escape "$name")"
    xversion="$(xml_escape "$version")"

    for format in $formats; do
        case " $used_formats " in
            *" $format "*) ;;
            *) used_formats="$used_formats $format" ;;
        esac
    done
    printf "%s    '%s': { name: '%s', version: '%s' }" "$separator" "$id" \
        "$(js_escape "$name")" "$(js_escape "$version")" >> "$frag/PLUGINS_JS"
    separator=",$nl"
    titles=""
    for format in $formats; do
        case "$format" in
            Standalone) titles="${titles:+$titles, }$(myzel_mac_title "$format")" ;;
            *) titles="${titles:+$titles, }$format" ;;
        esac
    done
    printf '<li><b>%s</b> %s: %s (%s)</li>\n' "$xname" "$xversion" "$(xml_escape "$blurb")" \
        "$(xml_escape "$titles")" >> "$frag/PLUGIN_LIST_HTML"
    printf '%s\t%s\n' "$id" "$name" >> "$frag/MYZEL_PLUGINS"

    install_outline="$install_outline            <line choice=\"install.$id\">$nl"
    install_choices="$install_choices    <choice id=\"install.$id\" title=\"$xname $xversion\"
        description=\"$(xml_escape "$(sentence "$blurb")") This installer carries $xname $xversion. It is ticked when $xname is missing on this Mac or an older or unknown version is installed, and unticked when $xversion or newer is already there; ticking it then reinstalls or downgrades. Open the row to choose formats.\"
        start_selected=\"myzelDefault('$id')\" enabled=\"myzelInstallEnabled('$id')\"/>$nl"

    for format in $formats; do
        key="$(myzel_mac_key "$format")"
        dir="$(myzel_mac_dir "$format")"
        ext="$(myzel_mac_ext "$format")"
        title="$(xml_escape "$(myzel_mac_title "$format")")"
        install_outline="$install_outline                <line choice=\"install.$id.$key\"/>$nl"
        install_choices="$install_choices    <choice id=\"install.$id.$key\" title=\"$title\"
        description=\"$xname as $title, in $(xml_escape "$dir"). Any copy already there is replaced.\"
        start_selected=\"myzelDefault('$id')\" selected=\"myzelInstallSelected('$id', '$key')\" enabled=\"myzelInstallEnabled('$id')\">
        <pkg-ref id=\"$receipt_prefix.$id.$key\"/>
    </choice>$nl"
        pkg_refs="$pkg_refs$nl    <pkg-ref id=\"$receipt_prefix.$id.$key\" version=\"$xversion\" onConclusion=\"none\">$id-$key.pkg</pkg-ref>"

        scripts="$work/scripts/$id-$key"
        mkdir -p "$scripts"
        cp "$here/scripts/preinstall" "$here/scripts/postinstall" "$scripts/"
        chmod 755 "$scripts/preinstall" "$scripts/postinstall"
        printf '%s\n%s\n' "$format" "$dir/$name$ext" > "$scripts/component"
    done
    install_outline="$install_outline            </line>$nl"

    remove_outline="$remove_outline            <line choice=\"remove.$id\"/>$nl"
    remove_choices="$remove_choices    <choice id=\"remove.$id\" title=\"Remove $xname\"
        description=\"Deletes $xname from this Mac in every format and forgets its receipts. Presets and settings stay. Ticking it unticks $xname under Install or update.\"
        start_selected=\"false\" selected=\"myzelRemoveSelected('$id')\" visible=\"myzelInstalled('$id')\">
        <pkg-ref id=\"$receipt_prefix.$id.remove\"/>
    </choice>$nl"
    pkg_refs="$pkg_refs$nl    <pkg-ref id=\"$receipt_prefix.$id.remove\" version=\"$suite\" onConclusion=\"none\">$id-remove.pkg</pkg-ref>"
done
echo >> "$frag/PLUGINS_JS"

separator=""
for format in $MYZEL_MAC_FORMATS; do
    printf "%s    '%s': { dir: '%s/', ext: '%s' }" "$separator" "$(myzel_mac_key "$format")" \
        "$(js_escape "$(myzel_mac_dir "$format")")" "$(myzel_mac_ext "$format")" >> "$frag/FORMATS_JS"
    separator=",$nl"
done
echo >> "$frag/FORMATS_JS"

for format in $MYZEL_MAC_FORMATS; do
    case " $used_formats " in
        *" $format "*) printf '<li>%s: <code>%s</code></li>\n' "$(xml_escape "$(myzel_mac_title "$format")")" \
            "$(xml_escape "$(myzel_mac_dir "$format")")" >> "$frag/LOCATIONS_HTML" ;;
    esac
done

cat > "$frag/OUTLINE" <<EOF
        <line choice="support"/>
        <line choice="install">
$install_outline        </line>
        <line choice="remove">
$remove_outline        </line>
EOF

cat > "$frag/CHOICES" <<EOF
    <choice id="support" title="Uninstaller" visible="false" start_selected="true" start_enabled="false"
        description="Installs $(xml_escape "$support_dir")/uninstall.sh, which removes plugins from Terminal.">
        <pkg-ref id="$receipt_prefix.support"/>
    </choice>
    <choice id="install" title="Install or update"
        description="Ticked plugins are installed or updated. A plugin starts ticked when it is missing on this Mac or an older or unknown version is installed."
        start_selected="myzelAnyDefault()"/>
$install_choices    <choice id="remove" title="Remove"
        description="Ticked plugins are deleted from this Mac in every format. Presets and settings stay."
        start_selected="false" visible="myzelAnyInstalled()"/>
$remove_choices
EOF
printf '%s\n' "$pkg_refs" > "$frag/PKG_REFS"

fill_template "$here/distribution.xml.in" "$work/distribution.xml"
fill_template "$here/resources/welcome.html" "$work/resources/welcome.html"
fill_template "$here/resources/conclusion.html" "$work/resources/conclusion.html"
cp "$MYZEL_ROOT/LICENSE" "$work/resources/license.txt"
fill_template "$here/uninstall.sh" "$work/uninstall.sh"
chmod 755 "$work/uninstall.sh"

if command -v xmllint >/dev/null; then
    xmllint --noout "$work/distribution.xml" || die "The generated Distribution is not well-formed XML."
fi

mkdir -p "$work/roots/support"
cp "$work/uninstall.sh" "$work/roots/support/uninstall.sh"
for id in $ids; do
    scripts="$work/scripts/$id-remove"
    mkdir -p "$scripts"
    cp "$here/scripts/remove" "$scripts/postinstall"
    cp "$work/uninstall.sh" "$scripts/uninstall.sh"
    chmod 755 "$scripts/postinstall" "$scripts/uninstall.sh"
    echo "$id" > "$scripts/plugin"
done

if [ "$generate_only" = 1 ]; then
    rm -rf "$work/roots" "$work/plists" "$work/packages"
    echo "Generated the pkg's parts in $work (no packages built)."
    exit 0
fi

# Ad-hoc signature, inner code first: nested Mach-O files and bundles, deepest first, then the bundle.
sign_bundle()
{
    local bundle="$1" main file
    main="$(/usr/libexec/PlistBuddy -c 'Print :CFBundleExecutable' "$bundle/Contents/Info.plist" 2>/dev/null || true)"
    find "$bundle" -mindepth 1 \( -type f -o -type d -name '*.*' \) | awk -F/ '{ print NF "\t" $0 }' \
        | sort -rn | cut -f2- > "$work/sign-list"
    while IFS= read -r file; do
        if [ -d "$file" ]; then
            [ -f "$file/Contents/Info.plist" ] || continue
        else
            [ -n "$main" ] && [ "$file" = "$bundle/Contents/MacOS/$main" ] && continue
            case "$(file -b "$file")" in
                *Mach-O*) ;;
                *) continue ;;
            esac
        fi
        codesign --force --sign - --timestamp=none "$file" >/dev/null
    done < "$work/sign-list"
    codesign --force --sign - --timestamp=none "$bundle"
    codesign --verify --strict --deep --verbose=2 "$bundle"
}

# Installer must write each bundle where the pkg says, not "relocate" onto a copy found elsewhere by
# bundle id (JUCE gives every format of a plugin the same one, so the VST3 would land in the AU).
# Version checks are off because a ticked row may reinstall or downgrade on purpose.
component_plist()
{
    local root="$1" plist="$2" i=0
    pkgbuild --analyze --root "$root" "$plist" >/dev/null
    while plutil -extract "$i" xml1 -o - "$plist" >/dev/null 2>&1; do
        plutil -replace "$i.BundleIsRelocatable" -bool NO "$plist"
        plutil -replace "$i.BundleIsVersionChecked" -bool NO "$plist"
        i=$((i + 1))
    done
    [ "$i" -gt 0 ] || die "pkgbuild --analyze found no bundle in $root."
}

for id in $ids; do
    name="$(myzel_plugin_name "$id")"
    version="$(myzel_stage_version "$stage" "$id")"
    for format in $(myzel_mac_stage_formats "$stage" "$id"); do
        key="$(myzel_mac_key "$format")"
        ext="$(myzel_mac_ext "$format")"
        source="$stage/$id/$format/$name$ext"
        [ -d "$source" ] || die "Missing $source (tools/stage.sh on macOS stages $format as $name$ext)."

        root="$work/roots/$id-$key"
        mkdir -p "$root"
        ditto "$source" "$root/$name$ext"
        xattr -cr "$root/$name$ext"
        chmod 755 "$root"
        chmod -R go-w,a+rX "$root/$name$ext"
        echo "Signing $id $format"
        sign_bundle "$root/$name$ext"

        plist="$work/plists/$id-$key.plist"
        component_plist "$root" "$plist"
        pkgbuild --root "$root" --component-plist "$plist" --identifier "$receipt_prefix.$id.$key" \
            --version "$version" --install-location "$(myzel_mac_dir "$format")" \
            --scripts "$work/scripts/$id-$key" "$work/packages/$id-$key.pkg"
    done
    pkgbuild --nopayload --scripts "$work/scripts/$id-remove" --identifier "$receipt_prefix.$id.remove" \
        --version "$suite" "$work/packages/$id-remove.pkg"
done

chmod 755 "$work/roots/support"
pkgbuild --root "$work/roots/support" --identifier "$receipt_prefix.support" --version "$suite" \
    --install-location "$support_dir" "$work/packages/support.pkg"

rm -f "$out/$product"
productbuild --distribution "$work/distribution.xml" --resources "$work/resources" \
    --package-path "$work/packages" "$out/$product"
echo "$out/$product"
