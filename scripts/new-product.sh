#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
# Creates a new product from one of the starter templates.
set -euo pipefail

usage() {
    cat <<'HELP'
Usage: scripts/new-product.sh "<Name>" [--from synth|effect] [--vendor "<Vendor>"] [--dry-run]

Copies plugins/startersynth (--from synth, the default) or
plugins/startereffect (--from effect) to plugins/<id> and renames it:
  "My Synth"  ->  folder/id "mysynth", classes "MySynth...", target "MySynth",
                  display name "My Synth" (hosts and the panel show it).
It generates new random plug-in class IDs, a new state magic and new Audio
Unit codes (a random subtype; the manufacturer code from the vendor),
regenerates the panel, and adds the product to the root CMakeLists.txt.

  --vendor      the company name hosts show (default: your git user.name);
                its first 4 letters are the Audio Unit manufacturer code
  --dry-run     print what would happen; change nothing
  --root DIR    work in another checkout (used by the tests)

Your plug-in must not be called "Kraftonic" (see TRADEMARKS.md), so the
name you pass replaces every "Kraftonic Starter ..." display string.
HELP
}

NAME="" FROM="synth" VENDOR="" DRY=0
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
while [ "$#" -gt 0 ]; do
    case "$1" in
        --from) FROM="${2:-}"; shift 2 ;;
        --vendor) VENDOR="${2:-}"; shift 2 ;;
        --dry-run) DRY=1; shift ;;
        --root) ROOT="$(cd "${2:-}" && pwd)"; shift 2 ;;
        -h|--help) usage; exit 0 ;;
        -*) usage >&2; exit 2 ;;
        *) [ -z "$NAME" ] || { usage >&2; exit 2; }; NAME="$1"; shift ;;
    esac
done
[ -n "$NAME" ] || { usage >&2; exit 2; }
case "$FROM" in
    synth) SRC_ID=startersynth SRC_PASCAL=StarterSynth SRC_DISPLAY="Starter Synth" AU_TYPE=aumu ;;
    effect) SRC_ID=startereffect SRC_PASCAL=StarterEffect SRC_DISPLAY="Starter Effect" AU_TYPE=aufx ;;
    *) echo "--from must be synth or effect" >&2; exit 2 ;;
esac

# --- Names ----------------------------------------------------------------
# Words are runs of letters and digits: "my  synth-2" -> My Synth 2.
DISPLAY="$(printf '%s' "$NAME" | tr -cs 'A-Za-z0-9' ' ' | sed 's/^ *//; s/ *$//')"
ID="$(printf '%s' "$DISPLAY" | tr -d ' ' | tr '[:upper:]' '[:lower:]')"
PASCAL=""
for word in $DISPLAY; do
    PASCAL="$PASCAL$(printf '%s' "${word:0:1}" | tr '[:lower:]' '[:upper:]')${word:1}"
done
UPPER="$(printf '%s' "$DISPLAY" | tr '[:lower:]' '[:upper:]')"
[ -z "$VENDOR" ] && VENDOR="$(git -C "$ROOT" config user.name 2>/dev/null || true)"
[ -z "$VENDOR" ] && VENDOR="Unnamed Vendor"
VENDOR_ID="$(printf '%s' "$VENDOR" | tr -cd 'A-Za-z0-9' | tr '[:upper:]' '[:lower:]')"

fail() { echo "error: $*" >&2; exit 1; }
[[ "$ID" =~ ^[a-z][a-z0-9]*$ ]] || fail "the name must start with a letter: \"$NAME\""
printf '%s %s' "$DISPLAY" "$VENDOR" | grep -qi kraftonic && fail "the name and vendor may not contain \"Kraftonic\" (TRADEMARKS.md)"
[ -d "$ROOT/plugins/$SRC_ID" ] || fail "template plugins/$SRC_ID is missing"
[ -e "$ROOT/plugins/$ID" ] && fail "plugins/$ID already exists"

# --- New permanent identity -------------------------------------------------
hex32() { printf '0x%su' "$(od -An -N4 -tx4 /dev/urandom | tr -d ' \n' | tr '[:lower:]' '[:upper:]')"; }
fuid() { printf '{%s, %s, %s, %s}' "$(hex32)" "$(hex32)" "$(hex32)" "$(hex32)"; }
PROCESSOR_FUID="$(fuid)" CONTROLLER_FUID="$(fuid)" STATE_MAGIC="$(hex32)"
# Audio Unit codes: a random lower-case subtype, and the vendor's first four
# letters as the manufacturer ("Example Audio" -> Exam, "Ab" -> Abxx). Apple
# reserves all-lower-case manufacturer codes, so the first letter is upper case.
AU_SUBTYPE="$(perl -e 'my @c = ("a".."z", 0..9); print map { $c[int rand @c] } 1..4')"
AU_MANUFACTURER="$(printf '%sxxxx' "$(printf '%s' "$VENDOR" | tr -cd 'A-Za-z' | tr '[:upper:]' '[:lower:]')" | cut -c1-4)"
AU_MANUFACTURER="$(printf '%s' "${AU_MANUFACTURER:0:1}" | tr '[:lower:]' '[:upper:]')${AU_MANUFACTURER:1}"
case "$AU_MANUFACTURER" in Kftc|Krft|Kraf) fail "the vendor gives the Audio Unit code $AU_MANUFACTURER, which is Kraftonic's; pick another --vendor" ;; esac

cat <<EOF
New product from plugins/$SRC_ID:
  folder         plugins/$ID
  display name   $DISPLAY (vendor: $VENDOR)
  classes        ${PASCAL}Engine, ${PASCAL}Parameters, ${PASCAL}Traits
  plug-in target $PASCAL ($PASCAL.vst3)
  processor id   $PROCESSOR_FUID
  controller id  $CONTROLLER_FUID
  state magic    $STATE_MAGIC
  audio unit     $AU_TYPE $AU_SUBTYPE $AU_MANUFACTURER (type, subtype, manufacturer)
EOF
[ "$DRY" = 1 ] && { echo "Dry run: nothing written."; exit 0; }

# --- Copy and rename ----------------------------------------------------------
DEST="$ROOT/plugins/$ID"
cp -R "$ROOT/plugins/$SRC_ID" "$DEST"
# File and folder names, deepest first so parents are renamed last.
find "$DEST" -depth \( -name "*$SRC_ID*" -o -name "*$SRC_PASCAL*" \) | while read -r path; do
    base="$(basename "$path")"
    base="${base//$SRC_PASCAL/$PASCAL}"
    mv "$path" "$(dirname "$path")/${base//$SRC_ID/$ID}"
done

# Contents: longest names first. perl -pi behaves the same on macOS and Linux.
SRC_UPPER="$(printf '%s' "$SRC_DISPLAY" | tr '[:lower:]' '[:upper:]')"
export DISPLAY_NAME="$DISPLAY" UPPER PASCAL ID VENDOR VENDOR_ID PROCESSOR_FUID CONTROLLER_FUID STATE_MAGIC
export AU_SUBTYPE AU_MANUFACTURER
export SRC_ID SRC_PASCAL SRC_DISPLAY SRC_UPPER
# shellcheck disable=SC2016 # perl, not the shell, expands $ENV{...} here
find "$DEST" -type f ! -name '*.png' -print0 | xargs -0 perl -pi -e '
    s/Kraftonic \Q$ENV{SRC_DISPLAY}\E/$ENV{DISPLAY_NAME}/g;
    s/KRAFTONIC \Q$ENV{SRC_UPPER}\E/$ENV{UPPER}/g;
    s/Kraftonic\Q$ENV{SRC_PASCAL}\E/$ENV{PASCAL}/g;
    s/com\.kraftonic\.\Q$ENV{SRC_ID}\E/com.$ENV{VENDOR_ID}.$ENV{ID}/g;
    s/\Q$ENV{SRC_PASCAL}\E/$ENV{PASCAL}/g;
    s/\Q$ENV{SRC_DISPLAY}\E/$ENV{DISPLAY_NAME}/g;
    s/\Q$ENV{SRC_UPPER}\E/$ENV{UPPER}/g;
    s/\Q$ENV{SRC_ID}\E/$ENV{ID}/g;
    s/(kVendor = )"Kraftonic"/$1"$ENV{VENDOR}"/;
    s/(COMPANY_NAME )"Kraftonic"/$1"$ENV{VENDOR}"/;
    s/(AU_SUBTYPE )"[A-Za-z0-9]{4}"/$1"$ENV{AU_SUBTYPE}"/;
    s/(AU_MANUFACTURER )"[A-Za-z0-9]{4}"/$1"$ENV{AU_MANUFACTURER}"/;
    s/(kVendorUrl = )"[^"]*"/$1""/;
    s/(kVendorEmail = )"[^"]*"/$1""/;
    $_ = "" if /^\s*wordmark="KRAFTONIC",$/;
    s/(kStateMagic = )0x[0-9A-Fa-f]{8}u/$1$ENV{STATE_MAGIC}/;
    s/(kProcessorFuid\[4\] = )\{[^}]*\}/$1$ENV{PROCESSOR_FUID}/;
    s/(kControllerFuid\[4\] = )\{[^}]*\}/$1$ENV{CONTROLLER_FUID}/;
'
echo "0.1.0" > "$DEST/VERSION"
# The template's own scaffolding test does not belong in the copy.
perl -0pi -e 's/\n# begin template-only.*?# end template-only\n//s' "$DEST/tests/CMakeLists.txt"

# The panel shows the product name, so regenerate it from the renamed script.
if command -v python3 >/dev/null 2>&1; then
    KRAFTONIC_UI_TOOLS="$ROOT/modules/ui/tools" python3 -B "$DEST/ui/make_${ID}_uidesc.py" >/dev/null
else
    echo "warning: python3 not found; run plugins/$ID/ui/make_${ID}_uidesc.py before building" >&2
fi

# Register the product after the last one in the root CMakeLists.txt.
perl -0pi -e 's/(.*\n([ \t]*)add_subdirectory\(plugins\/[^)]*\)\n)/$1$2add_subdirectory(plugins\/$ENV{ID})\n/s' "$ROOT/CMakeLists.txt"
grep -q "add_subdirectory(plugins/$ID)" "$ROOT/CMakeLists.txt" || fail "could not add plugins/$ID to CMakeLists.txt"

cat <<EOF

Created plugins/$ID. Next steps:
  1. Build it and run its tests:
       cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
       cmake --build build
       ctest --test-dir build -R '^($ID: |ui: $ID panel\$)'
     The plug-in is build/VST3/Release/$PASCAL.vst3: copy it to your VST3 folder.
     On macOS the Audio Unit is build/VST3/Release/$PASCAL.component: copy it
     to ~/Library/Audio/Plug-Ins/Components.
  2. Read plugins/$ID/README.md ("What to change first"), then change the
     parameter table, the engine and the presets.
  3. Retake the screenshot (plugins/$ID/screenshot.png):
       build/bin/Release/kraftonic_editor_snapshot build/VST3/Release/$PASCAL.vst3 plugins/$ID/screenshot.png
  4. Before a pull request: scripts/check.sh $ID
EOF
