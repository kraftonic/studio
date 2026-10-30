#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
#
# Build the Windows installer (Inno Setup 6) for every built Kraftonic VST3 plug-in.
# Runs in Git Bash (GitHub's windows runners) and works with any ISCC on PATH.
#
# Usage: packaging/windows/build-installer.sh <vst3-dir> <out-dir> [version]
#   <vst3-dir>  folder holding the built *.vst3 bundles (e.g. build/VST3/Release)
#   <out-dir>   where KraftonicStudio-<version>-Windows-x64-Setup.exe is written
#   [version]   defaults to the CMake project version
#
# Optional signing (skipped when unset; see packaging/README.md):
#   KRAFTONIC_WIN_CERT_PFX       path to a code-signing .pfx
#   KRAFTONIC_WIN_CERT_PASSWORD  its password
#   KRAFTONIC_WIN_TIMESTAMP_URL  RFC 3161 server (default http://timestamp.digicert.com)
#   ISCC                         path to ISCC.exe (default: PATH, then the standard install dirs)
set -euo pipefail
# Git Bash rewrites arguments that start with "/" (ISCC /D..., signtool /f) into
# Windows paths. Turn that off; we convert real paths ourselves with cygpath.
export MSYS_NO_PATHCONV=1 MSYS2_ARG_CONV_EXCL='*'
# shellcheck source=packaging/common.sh
source "$(dirname "$0")/../common.sh"

[ $# -ge 2 ] || die "usage: $0 <vst3-dir> <out-dir> [version]"
VST3_DIR="$1"
OUT_DIR="$2"
VERSION="${3:-$(kraftonic_version)}"
[[ "$VERSION" =~ ^[0-9]+(\.[0-9]+){1,3}$ ]] || die "version '$VERSION' is not numeric (x.y.z)"
HERE="$(cd "$(dirname "$0")" && pwd)"
PFX="${KRAFTONIC_WIN_CERT_PFX:-}"
TIMESTAMP_URL="${KRAFTONIC_WIN_TIMESTAMP_URL:-http://timestamp.digicert.com}"

find_iscc()
{
    if [ -n "${ISCC:-}" ]; then echo "$ISCC"; return; fi
    if command -v iscc > /dev/null 2>&1; then command -v iscc; return; fi
    if command -v ISCC.exe > /dev/null 2>&1; then command -v ISCC.exe; return; fi
    local d
    for d in "/c/Program Files (x86)/Inno Setup 6" "/c/Program Files/Inno Setup 6"; do
        if [ -x "$d/ISCC.exe" ]; then echo "$d/ISCC.exe"; return; fi
    done
    die "Inno Setup 6 (ISCC.exe) not found; install it (choco install innosetup) or set ISCC"
}

find_signtool()
{
    if command -v signtool.exe > /dev/null 2>&1; then command -v signtool.exe; return; fi
    local s
    # shellcheck disable=SC2012  # fixed SDK layout, no odd file names
    s="$(ls -1d "/c/Program Files (x86)/Windows Kits/10/bin/"*/x64/signtool.exe 2>/dev/null | LC_ALL=C sort -V | tail -1)"
    [ -n "$s" ] || die "signtool.exe not found (Windows SDK)"
    echo "$s"
}

winpath() { if command -v cygpath > /dev/null 2>&1; then cygpath -w "$1"; else echo "$1"; fi; }

bundles=()
while IFS= read -r b; do bundles+=("$b"); done < <(kraftonic_find_bundles "$VST3_DIR")
[ ${#bundles[@]} -gt 0 ] || die "no *.vst3 bundles in $VST3_DIR"

# Stage everything next to the script so all Source: paths in the .iss are relative.
WORK="$(mktemp -d "${TMPDIR:-/tmp}/kraftonic-inno.XXXXXX")"
trap 'rm -rf "$WORK"' EXIT
mkdir -p "$WORK/bundles" "$OUT_DIR"
cp "$HERE/kraftonic.iss" "$WORK/"
# Inno shows .txt files as-is; use CRLF so Notepad-era controls render line breaks.
sed 's/\r*$/\r/' "$KRAFTONIC_ROOT/LICENSE" > "$WORK/LICENSE.txt"

signtool=""
if [ -n "$PFX" ]; then
    [ -f "$PFX" ] || die "KRAFTONIC_WIN_CERT_PFX=$PFX does not exist"
    signtool="$(find_signtool)"
fi
sign_file()
{
    "$signtool" sign /f "$(winpath "$PFX")" /p "${KRAFTONIC_WIN_CERT_PASSWORD:-}" \
        /fd sha256 /tr "$TIMESTAMP_URL" /td sha256 "$(winpath "$1")"
}

components="[Components]"$'\r\n'
files="[Files]"$'\r\n'
uninstall="[UninstallDelete]"$'\r\n'
welcome="Kraftonic Studio $VERSION"$'\r\n\r\n'"This installer adds these plug-ins to C:\\Program Files\\Common Files\\VST3 (and CLAP):"$'\r\n\r\n'
for bundle in "${bundles[@]}"; do
    stem="$(kraftonic_bundle_stem "$bundle")"
    id="$(kraftonic_product_id "$bundle")"
    name="$(kraftonic_product_name "$bundle")"
    cp -R "$bundle" "$WORK/bundles/$stem.vst3"
    if [ -n "$signtool" ]; then
        while IFS= read -r bin; do
            echo "Signing $bin"; sign_file "$bin"
        done < <(find "$WORK/bundles/$stem.vst3/Contents" -type f \( -iname '*.vst3' -o -iname '*.dll' \))
    fi
    formats="VST3"
    files+="Source: \"bundles\\$stem.vst3\\*\"; DestDir: \"{app}\\$stem.vst3\"; Components: $id; Flags: ignoreversion recursesubdirs createallsubdirs"$'\r\n'
    # The CLAP plug-in (one file, <name>.clap next to the bundle) goes to Common Files\CLAP.
    clap="$(dirname "$bundle")/$stem.clap"
    if [ -f "$clap" ]; then
        cp "$clap" "$WORK/bundles/$stem.clap"
        [ -n "$signtool" ] && { echo "Signing $stem.clap"; sign_file "$WORK/bundles/$stem.clap"; }
        formats="VST3 + CLAP"
        files+="Source: \"bundles\\$stem.clap\"; DestDir: \"{commoncf64}\\CLAP\"; Components: $id; Flags: ignoreversion"$'\r\n'
    fi
    components+="Name: \"$id\"; Description: \"$name ($formats)\"; Types: full custom"$'\r\n'
    uninstall+="Type: dirifempty; Name: \"{app}\\$stem.vst3\""$'\r\n'
    welcome+="  - $name"$'\r\n'
    echo "Component $id: $name"
done
welcome+=$'\r\n'"Choose \"Choose plug-ins\" on the next pages to install only some of them. Rescan plug-ins in your host after installing. Uninstall from Settings > Apps."$'\r\n'
[ -z "$PFX" ] && welcome+=$'\r\n'"This build is not code-signed. Windows SmartScreen may warn before it runs."$'\r\n'
printf '%s\r\n%s\r\n%s' "$components" "$files" "$uninstall" > "$WORK/products.iss"
printf '%s' "$welcome" > "$WORK/welcome.txt"

# VersionInfoVersion must be numeric with up to 4 parts.
numeric="$VERSION"
while [ "$(echo "$numeric" | tr -cd . | wc -c | tr -d ' ')" -lt 3 ]; do numeric="$numeric.0"; done

iscc="$(find_iscc)"
args=("/DAppVersion=$VERSION" "/DAppVersionNumeric=$numeric")
if [ -n "$signtool" ]; then
    # $f is Inno's placeholder for the file it is signing (setup + uninstaller).
    # $q is Inno's quote character inside a SignTool definition.
    args+=("/DSignTool" "/Skraftonic=\$q$(winpath "$signtool")\$q sign /f \$q$(winpath "$PFX")\$q /p \$q${KRAFTONIC_WIN_CERT_PASSWORD:-}\$q /fd sha256 /tr $TIMESTAMP_URL /td sha256 \$f")
    echo "Signing installer and uninstaller"
else
    echo "Not signing (KRAFTONIC_WIN_CERT_PFX unset)"
fi
( cd "$WORK" && "$iscc" "${args[@]}" "$(winpath "$WORK/kraftonic.iss")" )

exe="$WORK/output/KraftonicStudio-$VERSION-Windows-x64-Setup.exe"
[ -f "$exe" ] || die "ISCC did not produce $exe"
cp "$exe" "$OUT_DIR/"
echo "Built $OUT_DIR/$(basename "$exe")"
