#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
#
# Validate EVERY built *.vst3 bundle with the Steinberg validator and pluginval.
# Every available product bundle is validated automatically.
#
# Usage: scripts/ci/validate-plugins.sh <vst3-dir> <validator> <logs-dir> [pluginval]
#   Without [pluginval], only the Steinberg validator runs.
# Writes <logs-dir>/validation.tsv (product, bundle, validator, pluginval) for
# scripts/ci/summarize.sh. Runs every product even if one fails; exits 1 if any failed.
#
# Per-OS behaviour (docs/KNOWN_ISSUES.md):
#   Linux: pluginval under xvfb-run with --skip-gui-tests and WITHOUT --vst3validator
#          (pluginval hands the validator the inner .so, which it rejects; the
#          validator runs on the bundle separately here anyway).
#   macOS/Windows: full pluginval suite, including GUI tests, with --vst3validator.
set -uo pipefail
# shellcheck source=packaging/common.sh
source "$(dirname "$0")/../../packaging/common.sh"

[ $# -ge 3 ] || die "usage: $0 <vst3-dir> <validator> <logs-dir> [pluginval]"
VST3_DIR="$1"
VALIDATOR="$2"
LOGS="$3"
PLUGINVAL="${4:-}"
STRICTNESS="${PLUGINVAL_STRICTNESS:-10}"
mkdir -p "$LOGS"
TSV="$LOGS/validation.tsv"
printf 'product\tbundle\tvalidator\tpluginval\n' > "$TSV"

os="$(uname -s)"
bundles=()
while IFS= read -r b; do bundles+=("$b"); done < <(kraftonic_find_bundles "$VST3_DIR")
if [ ${#bundles[@]} -eq 0 ]; then
    echo "No *.vst3 bundles in $VST3_DIR: nothing to validate."
    exit 0
fi

failed=0
for bundle in "${bundles[@]}"; do
    stem="$(kraftonic_bundle_stem "$bundle")"
    name="$(kraftonic_product_name "$bundle")"
    echo "::group::Steinberg validator: $name"
    vlog="$LOGS/$stem-steinberg-validator.log"
    "$VALIDATOR" "$bundle" > "$vlog" 2>&1
    vrc=$?
    tail -5 "$vlog"
    echo "::endgroup::"
    vresult="$(grep -Eo '[0-9]+ tests passed, [0-9]+ tests failed' "$vlog" | tail -1)"
    if [ $vrc -eq 0 ]; then vstatus="pass (${vresult:-ok})"; else vstatus="FAIL (${vresult:-exit $vrc})"; failed=1; fi

    pstatus="skipped"
    if [ -n "$PLUGINVAL" ]; then
        echo "::group::pluginval: $name"
        run=()
        extra=(--vst3validator "$VALIDATOR")
        case "$os" in
            Linux)
                run=(xvfb-run -a)
                extra=(--skip-gui-tests)
                ;;
        esac
        plog="$LOGS/$stem-pluginval.log"
        ${run[@]+"${run[@]}"} "$PLUGINVAL" --strictness-level "$STRICTNESS" --validate-in-process \
            "${extra[@]}" --output-dir "$LOGS/pluginval-$stem" "$bundle" > "$plog" 2>&1
        prc=$?
        tail -5 "$plog"
        echo "::endgroup::"
        note=""
        [ "$os" = Linux ] && note=", GUI tests skipped"
        if [ $prc -eq 0 ]; then pstatus="pass (strictness $STRICTNESS$note)"; else pstatus="FAIL (exit $prc)"; failed=1; fi
    fi
    printf '%s\t%s\t%s\t%s\n' "$name" "$(basename "$bundle")" "$vstatus" "$pstatus" >> "$TSV"
    echo "$name: validator $vstatus; pluginval $pstatus"
    [[ "$vstatus$pstatus" == *FAIL* ]] && echo "::error::$name failed validation (logs: $LOGS)"
done
exit $failed
