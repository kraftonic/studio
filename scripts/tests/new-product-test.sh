#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
# Test for scripts/new-product.sh: scaffolds a product from a starter in a
# temporary copy of the kit, then checks that no template name or identity
# is left and the panel matches its generator. Takes about a second: it
# doesn't compile the copy (GitHub CI builds everything).
#
# Usage: scripts/tests/new-product-test.sh synth|effect
set -euo pipefail
FROM="$1"
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
case "$FROM" in
    synth) SRC=startersynth ;;
    effect) SRC=startereffect ;;
    *) echo "usage: $0 synth|effect" >&2; exit 2 ;;
esac
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT
fail() { echo "FAIL: $*" >&2; exit 1; }

# A minimal copy of the kit: build files, modules, the starters and the script.
mkdir -p "$TMP/plugins" "$TMP/scripts"
cp -R "$ROOT/CMakeLists.txt" "$ROOT/cmake" "$ROOT/modules" "$TMP/"
cp -R "$ROOT/plugins/startersynth" "$ROOT/plugins/startereffect" "$TMP/plugins/"
cp "$ROOT/scripts/new-product.sh" "$TMP/scripts/"
perl -ni -e 'print unless /add_subdirectory\(plugins\/(?!starter)/' "$TMP/CMakeLists.txt"

"$TMP/scripts/new-product.sh" "Test Tone 2" --from "$FROM" --vendor "Example Audio" --root "$TMP" >/dev/null
DEST="$TMP/plugins/testtone2"
[ -d "$DEST" ] || fail "plugins/testtone2 was not created"
grep -q 'add_subdirectory(plugins/testtone2)' "$TMP/CMakeLists.txt" || fail "not added to CMakeLists.txt"

# Nothing named after the template is left, in file names or contents.
left="$(find "$DEST" -iname "*starter*"; grep -rIilE "starter ?(synth|effect)|template-only" "$DEST" || true)"
[ -z "$left" ] || fail "template names left in: $left"
TRAITS="$DEST/plugin/TestTone2Traits.h"
grep -q 'kPluginName = "Test Tone 2"' "$TRAITS" || fail "display name not set"
grep -q 'kVendor = "Example Audio"' "$TRAITS" || fail "vendor not set"

# The copy is the user's own code, outside the kit's namespace: it uses kit
# modules (kraftonic::core, <kraftonic/formats/...>) but is not one of them.
own="$(grep -rnE 'kraftonic::testtone2|namespace kraftonic|kraftonic/testtone2|kraftonic_testtone2' "$DEST" || true)"
[ -z "$own" ] || fail "the copy is still inside the kraftonic namespace: $own"

# Identity is new: no class id or state magic is shared with the template.
old="$(ls "$ROOT/plugins/$SRC/plugin/"*Traits.h)"
for hex in $(grep -E 'kStateMagic|Fuid\[4\]' "$old" | grep -oE '0x[0-9A-F]{8}u'); do
    grep -q "$hex" "$TRAITS" && fail "identity $hex copied from the template"
done
[ "$(grep -E 'kStateMagic|Fuid\[4\]' "$TRAITS" | grep -oE '0x[0-9A-F]{8}u' | wc -l | tr -d ' ')" = 9 ] || fail "expected 9 new identity words"

# New Audio Unit codes: a fresh subtype, and the manufacturer from --vendor.
au_code() { sed -nE "s/^[[:space:]]*$1 \"([A-Za-z0-9]{4})\"$/\1/p" "$2"; }
old_cmake="$ROOT/plugins/$SRC/CMakeLists.txt"
new_cmake="$DEST/CMakeLists.txt"
old_sub="$(au_code AU_SUBTYPE "$old_cmake")" old_manu="$(au_code AU_MANUFACTURER "$old_cmake")"
new_sub="$(au_code AU_SUBTYPE "$new_cmake")" new_manu="$(au_code AU_MANUFACTURER "$new_cmake")"
[ -n "$old_sub" ] && [ -n "$old_manu" ] || fail "no AU codes in the template's CMakeLists.txt"
[ -n "$new_sub" ] && [ "$new_sub" != "$old_sub" ] || fail "AU subtype '$new_sub' not new (template: $old_sub)"
[ "$new_manu" = Exam ] || fail "AU manufacturer '$new_manu' is not Exam (from --vendor \"Example Audio\")"
[ "$new_manu" != "$old_manu" ] || fail "AU manufacturer copied from the template"

# New bundle identifier, which is also the CLAP plug-in id (KRAFTONIC_CLAP_ID).
bundle_id() { sed -nE 's/^[[:space:]]*BUNDLE_IDENTIFIER "([^"]+)"$/\1/p' "$1"; }
old_id="$(bundle_id "$old_cmake")" new_id="$(bundle_id "$new_cmake")"
[ -n "$old_id" ] || fail "no BUNDLE_IDENTIFIER in the template's CMakeLists.txt"
[ "$new_id" = com.exampleaudio.testtone2 ] || fail "bundle/CLAP id '$new_id' is not com.exampleaudio.testtone2"
[ "$new_id" != "$old_id" ] || fail "bundle/CLAP id copied from the template"
grep -q 'KRAFTONIC_CLAP_ENTRY (TestTone2Traits\|KRAFTONIC_CLAP_ENTRY (testtone2::plugin::TestTone2Traits' "$DEST"/plugin/*Entry.cpp \
    || fail "no CLAP entry for the copy's traits"

# The panel was regenerated with the new name and still passes its checks.
if command -v python3 >/dev/null 2>&1; then
    python3 -B "$TMP/modules/ui/tools/check_panel.py" "$DEST/ui/testtone2.uidesc" \
        --generator "$DEST/ui/make_testtone2_uidesc.py" \
        --params-header "$DEST/dsp/TestTone2Parameters.h" >/dev/null || fail "panel check"
    grep -q '"TEST TONE 2"' "$DEST/ui/testtone2.uidesc" || fail "panel title not renamed"
fi

echo "PASS: new-product.sh --from $FROM makes a renamed product with a new identity"
