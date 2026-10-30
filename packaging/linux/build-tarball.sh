#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
#
# Build the Linux .tar.gz for every built Kraftonic VST3 plug-in (and its .clap
# when one was built), with README and install.sh.
# Usage: packaging/linux/build-tarball.sh <vst3-dir> <out-dir> [version]
# Output: <out-dir>/KraftonicStudio-<version>-Linux-<arch>.tar.gz
set -euo pipefail
# shellcheck source=packaging/common.sh
source "$(dirname "$0")/../common.sh"

[ $# -ge 2 ] || die "usage: $0 <vst3-dir> <out-dir> [version]"
VST3_DIR="$1"
OUT_DIR="$2"
VERSION="${3:-$(kraftonic_version)}"
[[ "$VERSION" =~ ^[0-9]+(\.[0-9]+){1,3}$ ]] || die "version '$VERSION' is not numeric (x.y.z)"
HERE="$(cd "$(dirname "$0")" && pwd)"

bundles=()
while IFS= read -r b; do bundles+=("$b"); done < <(kraftonic_find_bundles "$VST3_DIR")
[ ${#bundles[@]} -gt 0 ] || die "no *.vst3 bundles in $VST3_DIR"

# Architecture from the bundle's Contents/<arch>-linux folder (what hosts look for).
arch="$(basename "$(find "${bundles[0]}/Contents" -maxdepth 1 -type d -name '*-linux' | head -1)")"
arch="${arch%-linux}"
[ -n "$arch" ] || die "${bundles[0]} has no Contents/<arch>-linux folder (not a Linux build?)"

NAME="KraftonicStudio-$VERSION-Linux-$arch"
WORK="$(mktemp -d "${TMPDIR:-/tmp}/kraftonic-tar.XXXXXX")"
trap 'rm -rf "$WORK"' EXIT
mkdir -p "$WORK/$NAME" "$OUT_DIR"

products=""
for bundle in "${bundles[@]}"; do
    cp -R "$bundle" "$WORK/$NAME/"
    clap="$(dirname "$bundle")/$(basename "$bundle" .vst3).clap"
    [ -f "$clap" ] && cp "$clap" "$WORK/$NAME/"
    products+="    $(kraftonic_product_name "$bundle")  ($(basename "$bundle"))"$'\n'
    echo "Added $(basename "$bundle")"
done
products="${products%$'\n'}"
install -m 755 "$HERE/install.sh" "$WORK/$NAME/install.sh"
cp "$KRAFTONIC_ROOT/LICENSE" "$WORK/$NAME/LICENSE"
awk -v v="$VERSION" -v a="$arch" -v p="$products" \
    '{ gsub(/@VERSION@/, v); gsub(/@ARCH@/, a); if ($0 == "@PRODUCTS@") print p; else print }' \
    "$HERE/README.txt" > "$WORK/$NAME/README.txt"

OUT="$(cd "$OUT_DIR" && pwd)/$NAME.tar.gz"
# Reproducible-ish: fixed owner, sorted entries (GNU tar); plain tar elsewhere.
if tar --version 2> /dev/null | grep -q GNU; then
    tar -C "$WORK" --owner=0 --group=0 --numeric-owner --sort=name -czf "$OUT" "$NAME"
else
    COPYFILE_DISABLE=1 tar -C "$WORK" -czf "$OUT" "$NAME"
fi
echo "Built $OUT"
