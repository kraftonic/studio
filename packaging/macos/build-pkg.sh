#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
#
# Build the macOS installer (.pkg) for every built Kraftonic VST3 plug-in,
# with its Audio Unit (<name>.component next to <name>.vst3) and CLAP plug-in
# (<name>.clap) when they were built.
#
# Usage: packaging/macos/build-pkg.sh [--product <id>] <vst3-dir> <out-dir> [version]
#   --product <id>  package only this product (a folder under plugins/)
#   <vst3-dir>  folder holding the built *.vst3 bundles (e.g. build/mac/VST3/Release)
#   <out-dir>   where KraftonicStudio-<version>-macOS.pkg is written
#   [version]   defaults to the CMake project version
#
# Optional signing (skipped when unset; see packaging/README.md):
#   KRAFTONIC_MAC_APP_IDENTITY        "Developer ID Application: ..." - signs each bundle
#   KRAFTONIC_MAC_INSTALLER_IDENTITY  "Developer ID Installer: ..."   - signs the .pkg
#   KRAFTONIC_MAC_KEYCHAIN            keychain holding those identities
# Notarize the signed .pkg yourself afterwards (xcrun notarytool submit --wait, then xcrun stapler staple).
# Works with macOS's bash 3.2 (also what GitHub's macOS runners use for `shell: bash`).
set -euo pipefail
# shellcheck source=packaging/common.sh
source "$(dirname "$0")/../common.sh"

ONLY=""
while [ $# -gt 0 ]; do
    case "$1" in
        --product) [ $# -ge 2 ] || die "--product needs an id"; ONLY="$2"; shift 2 ;;
        --*) die "unknown option: $1" ;;
        *) break ;;
    esac
done
[ $# -ge 2 ] || die "usage: $0 [--product <id>] <vst3-dir> <out-dir> [version]"
VST3_DIR="$1"
OUT_DIR="$2"
VERSION="${3:-$(kraftonic_version)}"
PACKAGE_STEM=KraftonicStudio
PACKAGE_NAME="Kraftonic Studio"
if [ -n "$ONLY" ]; then
    [ -f "$KRAFTONIC_ROOT/plugins/$ONLY/VERSION" ] || die "unknown product: $ONLY"
    VERSION="${3:-$(kraftonic_product_version "$ONLY")}"
fi
[[ "$VERSION" =~ ^[0-9]+(\.[0-9]+){1,3}$ ]] || die "version '$VERSION' is not numeric (x.y.z)"
APP_ID="${KRAFTONIC_MAC_APP_IDENTITY:-}"
INSTALLER_ID="${KRAFTONIC_MAC_INSTALLER_IDENTITY:-}"
KEYCHAIN="${KRAFTONIC_MAC_KEYCHAIN:-}"
INSTALL_LOCATION="/Library/Audio/Plug-Ins" # VST3/, Components/ and CLAP/
PKG_PREFIX="com.kraftonic.studio"
HERE="$(cd "$(dirname "$0")" && pwd)"

bundles=()
while IFS= read -r b; do
    if [ -z "$ONLY" ] || [ "$(kraftonic_product_id "$b")" = "$ONLY" ]; then bundles+=("$b"); fi
done < <(kraftonic_find_bundles "$VST3_DIR")
[ ${#bundles[@]} -gt 0 ] || die "no *.vst3 bundles in $VST3_DIR"

if [ -n "$ONLY" ]; then
    PACKAGE_STEM="$(kraftonic_bundle_stem "${bundles[0]}")"
    PACKAGE_NAME="$(kraftonic_product_name "${bundles[0]}")"
fi

WORK="$(mktemp -d "${TMPDIR:-/tmp}/kraftonic-pkg.XXXXXX")"
trap 'rm -rf "$WORK"' EXIT
mkdir -p "$WORK/components" "$WORK/resources" "$OUT_DIR"

keychain_args=()
[ -n "$KEYCHAIN" ] && keychain_args=(--keychain "$KEYCHAIN")

choices=""
outline=""
product_list=""
for bundle in "${bundles[@]}"; do
    stem="$(kraftonic_bundle_stem "$bundle")"
    id="$(kraftonic_product_id "$bundle")"
    name="$(kraftonic_product_name "$bundle")"
    pkg_id="$PKG_PREFIX.$id.vst3"
    root="$WORK/root-$id"
    mkdir -p "$root"
    # Never ship a bundle whose moduleinfo.json or Info.plist version is wrong.
    mi="$bundle/Contents/Resources/moduleinfo.json"
    mi_ver="$(kraftonic_moduleinfo_version "$mi" 2> /dev/null)" \
        || die "$stem: $mi is missing, empty or not valid JSON"
    # Each bundle carries its product's version (plugins/<id>/VERSION).
    want_ver="$(kraftonic_product_version "$id")"
    [ "$mi_ver" = "$want_ver" ] || die "$stem: moduleinfo.json version '$mi_ver' is not $want_ver"
    plist_ver="$(/usr/libexec/PlistBuddy -c 'Print :CFBundleShortVersionString' "$bundle/Contents/Info.plist" 2> /dev/null || true)"
    [ "$plist_ver" = "$want_ver" ] || die "$stem: Info.plist version '$plist_ver' is not $want_ver"
    mkdir -p "$root/VST3"
    payload=("VST3/$stem.vst3")
    # Copy without extended attributes / resource forks (com.apple.provenance,
    # quarantine...), or pkgbuild stores them as "._*" AppleDouble files.
    # Code signatures live in the Mach-O and _CodeSignature/, so they survive.
    ditto --norsrc --noextattr --noqtn --noacl "$bundle" "$root/VST3/$stem.vst3"
    # The Audio Unit wraps a copy of the VST3 (Contents/Resources/plugin.vst3).
    component="$(dirname "$bundle")/$stem.component"
    if [ -d "$component" ]; then
        mkdir -p "$root/Components"
        ditto --norsrc --noextattr --noqtn --noacl "$component" "$root/Components/$stem.component"
        payload+=("Components/$stem.component/Contents/Resources/plugin.vst3" "Components/$stem.component")
    fi
    clap="$(dirname "$bundle")/$stem.clap"
    if [ -d "$clap" ]; then
        mkdir -p "$root/CLAP"
        ditto --norsrc --noextattr --noqtn --noacl "$clap" "$root/CLAP/$stem.clap"
        payload+=("CLAP/$stem.clap")
    fi
    # Best effort: com.apple.provenance (added on some dev machines) cannot be removed.
    xattr -cr "$root" 2> /dev/null || true

    for item in "${payload[@]}"; do
        if [ -n "$APP_ID" ]; then
            echo "Signing $item with '$APP_ID'"
            codesign --force --timestamp --options runtime ${keychain_args[@]+"${keychain_args[@]}"} \
                --sign "$APP_ID" "$root/$item"
        else
            # No identity: seal the bundle ad hoc. The linker only signs the
            # Mach-O, which leaves the bundle itself "not signed at all"; some
            # hosts and Gatekeeper treat that worse than an ad-hoc seal.
            echo "Ad-hoc signing $item (KRAFTONIC_MAC_APP_IDENTITY unset)"
            codesign --force --deep --sign - "$root/$item"
        fi
        codesign --verify --strict --verbose=2 "$root/$item"
    done

    # Never let Installer "relocate" the bundle to another copy with the same
    # bundle id (e.g. a dev build elsewhere on disk): always install in place.
    # No version check either: running an installer installs exactly its version.
    plist="$WORK/components-$id.plist"
    pkgbuild --analyze --root "$root" "$plist" > /dev/null
    # One entry per top-level bundle (the AU's inner plugin.vst3 is a child entry).
    for ((i = 0; ; i++)); do
        plutil -extract "$i" xml1 -o /dev/null "$plist" > /dev/null 2>&1 || break
        plutil -replace "$i.BundleIsRelocatable" -bool NO "$plist"
        plutil -replace "$i.BundleIsVersionChecked" -bool NO "$plist"
        plutil -replace "$i.BundleOverwriteAction" -string upgrade "$plist"
    done

    pkgbuild --root "$root" --component-plist "$plist" \
        --install-location "$INSTALL_LOCATION" \
        --identifier "$pkg_id" --version "$VERSION" \
        "$WORK/components/$id.pkg" > /dev/null
    echo "Component $id.pkg: $name ($pkg_id)"

    choices+="    <choice id=\"$id\" title=\"$name\" description=\"$name: VST3, Audio Unit and CLAP plug-ins.\" start_selected=\"true\">
        <pkg-ref id=\"$pkg_id\"/>
    </choice>
    <pkg-ref id=\"$pkg_id\" version=\"$VERSION\" onConclusion=\"none\">$id.pkg</pkg-ref>
"
    outline+="        <line choice=\"$id\"/>
"
    product_list+="<li>$name</li>"
done

unsigned_note=""
[ -z "$INSTALLER_ID" ] && unsigned_note="<p><i>This build is not signed or notarized. If macOS blocks it, Control-click the .pkg and choose Open.</i></p>"
sed -e "s|@VERSION@|$VERSION|g" -e "s|@PRODUCTS@|$product_list|" -e "s|@UNSIGNED_NOTE@|$unsigned_note|" \
    "$HERE/resources/welcome.html" > "$WORK/resources/welcome.html"
# Licence step: Kraftonic's licence, third-party notices and
# the full third-party licence texts.
SDK_DIR="${KRAFTONIC_VST3SDK_DIR:-}"
if [ -z "$SDK_DIR" ]; then
    for d in "$HOME/.cache/kraftonic/sdk381/vst3sdk" "$KRAFTONIC_ROOT/.deps/vst3sdk-src" \
        "$KRAFTONIC_ROOT/build/_deps/vst3sdk-src"; do
        [ -f "$d/LICENSE.txt" ] && { SDK_DIR="$d"; break; }
    done
fi
[ -f "$SDK_DIR/LICENSE.txt" ] && [ -f "$SDK_DIR/vstgui4/LICENSE" ] \
    || die "VST3 SDK licence texts not found (set KRAFTONIC_VST3SDK_DIR to the vst3sdk source folder)"
# The Audio Units contain Apple's AudioUnitSDK (Apache-2.0): its licence is needed when one ships.
AUSDK_LICENSE=""
if find "$WORK" -maxdepth 3 -name '*.component' | grep -q .; then
    for d in "${KRAFTONIC_AUDIOUNITSDK_DIR:-}" "$VST3_DIR/../../_deps/audiounitsdk-src" \
        "$KRAFTONIC_ROOT/.deps/audiounitsdk-src" "$KRAFTONIC_ROOT/build/_deps/audiounitsdk-src"; do
        [ -n "$d" ] && [ -f "$d/LICENSE.txt" ] && { AUSDK_LICENSE="$d/LICENSE.txt"; break; }
    done
    [ -n "$AUSDK_LICENSE" ] \
        || die "AudioUnitSDK licence not found (set KRAFTONIC_AUDIOUNITSDK_DIR to the AudioUnitSDK source folder)"
fi
{
    cat "$KRAFTONIC_ROOT/LICENSE"
    printf '\n\n==============================================================\n\n'
    cat "$KRAFTONIC_ROOT/THIRD_PARTY_NOTICES.md"
    printf '\n\n==============================================================\nSteinberg VST3 SDK licence\n\n'
    cat "$SDK_DIR/LICENSE.txt"
    printf '\n\n==============================================================\nVSTGUI licence\n\n'
    cat "$SDK_DIR/vstgui4/LICENSE"
    if [ -n "$AUSDK_LICENSE" ]; then
        printf '\n\n==============================================================\nApple AudioUnitSDK licence\n\n'
        cat "$AUSDK_LICENSE"
    fi
} > "$WORK/resources/LICENSE.txt"

# Minimum macOS comes from the deployment target the plug-ins are built for.
MIN_OS="${KRAFTONIC_MAC_MIN_OS:-11.0}"
cat > "$WORK/distribution.xml" <<XML
<?xml version="1.0" encoding="utf-8"?>
<installer-gui-script minSpecVersion="2">
    <title>$PACKAGE_NAME $VERSION</title>
    <organization>$PKG_PREFIX</organization>
    <welcome file="welcome.html" mime-type="text/html"/>
    <license file="LICENSE.txt" mime-type="text/plain"/>
    <options customize="allow" require-scripts="false" hostArchitectures="arm64,x86_64" rootVolumeOnly="true"/>
    <domains enable_localSystem="true" enable_currentUserHome="false" enable_anywhere="false"/>
    <volume-check>
        <allowed-os-versions><os-version min="$MIN_OS"/></allowed-os-versions>
    </volume-check>
    <choices-outline>
$outline    </choices-outline>
$choices</installer-gui-script>
XML

OUT="$OUT_DIR/$PACKAGE_STEM-$VERSION-macOS.pkg"
sign_args=()
if [ -n "$INSTALLER_ID" ]; then
    echo "Signing installer with '$INSTALLER_ID'"
    sign_args=(--sign "$INSTALLER_ID" --timestamp ${keychain_args[@]+"${keychain_args[@]}"})
else
    echo "Not signing installer (KRAFTONIC_MAC_INSTALLER_IDENTITY unset)"
fi
productbuild --distribution "$WORK/distribution.xml" --resources "$WORK/resources" \
    --package-path "$WORK/components" --version "$VERSION" ${sign_args[@]+"${sign_args[@]}"} "$OUT"
[ -n "$INSTALLER_ID" ] && pkgutil --check-signature "$OUT"
echo "Built $OUT"
