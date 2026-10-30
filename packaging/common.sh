# SPDX-License-Identifier: MIT
#
# Shared helpers for the packaging scripts. Source it; don't run it.
# Every product is discovered from the built *.vst3 bundles.

KRAFTONIC_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

die() { echo "error: $*" >&2; exit 1; }

# Project version from the root CMakeLists.txt.
kraftonic_version()
{
    cmake -P "$KRAFTONIC_ROOT/cmake/KraftonicPrintVersion.cmake" | tr -d '\r'
}

# Version of one product from plugins/<id>/VERSION.
# Falls back to the project version for a product without a VERSION file.
kraftonic_product_version()
{
    local file="$KRAFTONIC_ROOT/plugins/$1/VERSION"
    if [ -f "$file" ]; then
        tr -d ' \r\n' < "$file"; echo
    else
        kraftonic_version
    fi
}

# Print every shippable *.vst3 bundle directly inside $1 (or one level below),
# sorted, one per line. Test-only plug-ins (*SelfTest*) are never packaged.
kraftonic_find_bundles()
{
    find "$1" -maxdepth 2 -type d -name '*.vst3' ! -name '*SelfTest*' -prune 2>/dev/null | LC_ALL=C sort
}

# KraftonicStarterSynth.vst3 -> KraftonicStarterSynth
kraftonic_bundle_stem() { basename "$1" .vst3; }

# KraftonicStarterSynth.vst3 -> startersynth   (stable id: used in package ids / component names)
kraftonic_product_id()
{
    local stem; stem="$(kraftonic_bundle_stem "$1")"
    stem="${stem#Kraftonic}"
    echo "$stem" | tr '[:upper:]' '[:lower:]' | tr -cd 'a-z0-9'
}

# KraftonicStarterSynth.vst3 -> Kraftonic Starter Synth   (display name)
kraftonic_product_name()
{
    kraftonic_bundle_stem "$1" | sed -E 's/([a-z0-9])([A-Z])/\1 \2/g'
}

# Version field of a VST3 moduleinfo.json. The SDK writes JSON5 (trailing
# commas), so strip those before parsing. Fails if the file is missing,
# empty or not valid.
kraftonic_moduleinfo_version()
{
    python3 -I -c 'import json, re, sys
text = open(sys.argv[1], encoding="utf-8").read()
print(json.loads(re.sub(r",(\s*[}\]])", r"\1", text))["Version"])' "$1"
}
