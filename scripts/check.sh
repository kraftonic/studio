#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
# Per-product native merge checks. GitHub Actions runs the macOS, Windows and Linux suite.
set -uo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT" || exit 1

usage() {
    cat <<'HELP'
Usage: scripts/check.sh <product|all> [--full] [--help]   (product = a folder under plugins/)
Runs native checks and prints RESULT: PASS or RESULT: FAIL.
  <product>   build + that product's tests, real-time test, the Steinberg
              validator and clap-validator on its CLAP plug-in; on macOS also
              auval on its Audio Unit. Module tests run only when modules/ changed.
  all         every product and every module test.
  --full      also pluginval (strictness 10) on each plug-in. Slow and CPU-heavy;
              GitHub Actions always runs it on pull requests.
Shared core modules or build infrastructure changes widen the check to all.
The changes are measured from the merge base with KRAFTONIC_CHECK_BASE, or the
nearest of upstream/main, origin/main, main, develop.
HELP
}

if [ "$#" -eq 0 ] || [ "$1" = "--help" ] || [ "$1" = "-h" ]; then
    usage
    [ "$#" -eq 0 ] && exit 2
    exit 0
fi
PRODUCT="$1"
REQUESTED_PRODUCT="$PRODUCT"
shift
FULL=0
for arg in "$@"; do
    case "$arg" in
        --full) FULL=1 ;;
        *) usage >&2; exit 2 ;;
    esac
done
case "$PRODUCT" in all) ;; *) [ -d "plugins/$PRODUCT" ] || { echo "unknown product: $PRODUCT" >&2; usage >&2; exit 2; } ;; esac

# One check at a time per machine: parallel checks overload the computer and
# make the real-time speed tests fail. Others wait here. A lock left by a
# process that no longer exists is removed. KRAFTONIC_CHECK_NO_LOCK=1 skips it.
LOCK_DIR="${KRAFTONIC_CHECK_LOCK:-$HOME/.cache/kraftonic/check.lock}"
if [ "${KRAFTONIC_CHECK_NO_LOCK:-0}" != 1 ]; then
    mkdir -p "$(dirname "$LOCK_DIR")"
    waited=0
    until mkdir "$LOCK_DIR" 2>/dev/null; do
        holder="$(cat "$LOCK_DIR/pid" 2>/dev/null || true)"
        if [ -n "$holder" ] && ! kill -0 "$holder" 2>/dev/null; then
            rm -rf "$LOCK_DIR"
            continue
        fi
        if [ "$waited" -eq 0 ]; then
            echo "Another check is running (pid ${holder:-?}); waiting for it to finish."
        fi
        waited=1
        sleep 15
    done
    echo $$ > "$LOCK_DIR/pid"
    trap 'rm -rf "$LOCK_DIR"' EXIT
fi

BUILD="$ROOT/build/check"
LOGS="$BUILD/logs"
mkdir -p "$LOGS"
# Everything printed below is also kept in $SUMMARY, so a reviewer can reuse an
# author's result when the commit hash in its header matches the branch head.
SUMMARY="$LOGS/summary-$REQUESTED_PRODUCT.txt"
: > "$SUMMARY"
say() { echo "$*" | tee -a "$SUMMARY"; }

# The base the changes are measured from: the candidate HEAD is fewest commits ahead of.
BASE=""
BASE_REF=""
best=""
for ref in ${KRAFTONIC_CHECK_BASE:-upstream/main origin/main main develop}; do
    mb="$(git merge-base HEAD "$ref" 2>/dev/null)" || continue
    ahead="$(git rev-list --count "$mb..HEAD")"
    if [ -z "$best" ] || [ "$ahead" -lt "$best" ]; then
        best="$ahead"; BASE="$mb"; BASE_REF="$ref"
    fi
done

HEAD_HASH="$(git rev-parse HEAD)"
DIRTY=""
[ -n "$(git status --porcelain --untracked-files=normal 2>/dev/null)" ] && DIRTY=" + uncommitted changes (not reusable)"
full_flag=""
[ "$FULL" = 1 ] && full_flag=" --full"
say "Check: $REQUESTED_PRODUCT$full_flag"
say "Commit: $HEAD_HASH$DIRTY"
say "Base: ${BASE_REF:-none} ${BASE:-(no base found: checking as all)}"
say "Date: $(date -u '+%Y-%m-%dT%H:%M:%SZ')"

# Widening: shared build infrastructure or core modules -> every product;
# another module (synth, piano, ui, ...) -> only the products that use it.
WIDEN_REASON=""
EXTRA_PRODUCTS=()
CHANGED=""
if [ -n "$BASE" ]; then
    CHANGED="$( { git diff --name-only "$BASE"; git ls-files --others --exclude-standard; } | sort -u)"
    if printf '%s\n' "$CHANGED" | grep -Eq '^(modules/(core|fx|sampler|formats)/|cmake/|CMakeLists\.txt$|scripts/ci/|packaging/)'; then
        WIDEN_REASON="core modules, build configuration, CI, or packaging changed"
        PRODUCT=all
    else
        for m in $(printf '%s\n' "$CHANGED" | sed -n 's#^modules/\([^/]*\)/.*#\1#p' | sort -u); do
            # The folder name is the library name: modules/<m> -> kraftonic::<m>.
            users="$(grep -rlE "kraftonic::$m([^A-Za-z_]|$)|modules/$m/" plugins/*/CMakeLists.txt plugins/*/tests/CMakeLists.txt plugins/*/ui/*.py 2>/dev/null \
                | sed -n 's#^plugins/\([^/]*\)/.*#\1#p')"
            users="$(printf '%s\n' "$users" | sort -u | tr '\n' ' ')"
            [ -n "${users// /}" ] && say "modules/$m changed: also checking its users: $users"
            for u in $users; do EXTRA_PRODUCTS+=("$u"); done
        done
    fi
else
    WIDEN_REASON="no base branch found to compare against"
    PRODUCT=all
fi
if [ -n "$WIDEN_REASON" ]; then
    say "Widening check to all: $WIDEN_REASON."
fi
# Module tests only when a module changed, or for all.
RUN_MODULES=0
if [ "$PRODUCT" = all ] || printf '%s\n' "$CHANGED" | grep -q '^modules/'; then RUN_MODULES=1; fi

FAILED=0
pass() { say "PASS: $*"; }
fail() { say "FAIL: $*"; FAILED=1; }
finish() {
    if [ "$FAILED" -eq 0 ]; then say "RESULT: PASS"; exit 0; fi
    say "RESULT: FAIL"
    exit 1
}
run_logged() {
    local label="$1" log="$2"
    shift 2
    if "$@" >"$log" 2>&1; then
        pass "$label"
        return 0
    fi
    fail "$label (see ${log#"$ROOT"/})"
    tail -20 "$log"
    return 1
}

SDK_CACHE="${HOME}/.cache/kraftonic/sdk381/vst3sdk"
sdk_args=()
[ -d "$SDK_CACHE" ] && sdk_args=("-DFETCHCONTENT_SOURCE_DIR_VST3SDK=$SDK_CACHE")
if ! run_logged "Release configure (native arch, Ninja, warnings as errors)" "$LOGS/configure.log" \
    cmake -S . -B "$BUILD" -G Ninja -DCMAKE_BUILD_TYPE=Release \
    -DKRAFTONIC_WARNINGS_AS_ERRORS=ON "${sdk_args[@]}"; then
    finish
fi

module_targets=(kraftonic_core_tests kraftonic_fx_tests kraftonic_fx_realtime \
    kraftonic_sampler_tests kraftonic_sampler_realtime kraftonic_synth_tests kraftonic_synth_realtime \
    kraftonic_piano_tests kraftonic_piano_realtime \
    kraftonic_wavetable_tests kraftonic_wavetable_realtime \
    kraftonic_formats_tests kraftonic_formats_vst3_tests kraftonic_formats_clap_tests)
case "$PRODUCT" in
    all) products=(); for d in plugins/*/; do d="${d%/}"; products+=("${d##*/}"); done ;;
    *) products=("$PRODUCT") ;;
esac
for e in ${EXTRA_PRODUCTS[@]+"${EXTRA_PRODUCTS[@]}"}; do
    case " ${products[*]} " in *" $e "*) ;; *) products+=("$e") ;; esac
done
# The plug-in target named in the product's kraftonic_add_plugin() (or
# smtg_add_vst3plugin()); folder names like "startersynth" do not title-case
# to "KraftonicStarterSynth".
plugin_target() {
    local t
    t="$(sed -nE 's/^[[:space:]]*(kraftonic_add_plugin|smtg_add_vst3plugin)\(([A-Za-z0-9_]*).*/\2/p' "plugins/$1/CMakeLists.txt" 2>/dev/null | head -1)"
    [ -n "$t" ] || t="Kraftonic$(printf '%s' "$1" | cut -c1 | tr '[:lower:]' '[:upper:]')${1:1}"
    printf '%s' "$t"
}
MACOS=0
[ "$(uname -s)" = Darwin ] && MACOS=1
build_targets=(validator)
[ "$RUN_MODULES" = 1 ] && build_targets+=("${module_targets[@]}")
for p in ${products[@]+"${products[@]}"}; do
    build_targets+=("$(plugin_target "$p")" "$(plugin_target "$p")_CLAP" "${p}_tests" "${p}_realtime" "${p}_plugin_tests")
    [ "$MACOS" = 1 ] && build_targets+=("$(plugin_target "$p")_AU")
done
if ! run_logged "build targets: ${build_targets[*]}" "$LOGS/build.log" \
    cmake --build "$BUILD" --target "${build_targets[@]}"; then
    finish
fi

realtime_owners=""
if [ "$RUN_MODULES" = 1 ]; then
    module_pattern='^(core|fx|sampler|synth|piano|wavetable|formats|ui): '
    if ! run_logged "module tests (core/fx/sampler/synth/piano/wavetable/formats/ui prefixes)" "$LOGS/ctest-modules.log" \
        ctest --test-dir "$BUILD" --output-on-failure -R "$module_pattern"; then :; fi
    realtime_owners="core|fx|sampler|synth|piano|wavetable|formats|ui"
else
    say "Module tests skipped: no change under modules/."
fi
for p in ${products[@]+"${products[@]}"}; do
    # A product's panel test is registered by modules/ui as "ui: <p> panel".
    if ! run_logged "$p tests" "$LOGS/ctest-$p.log" \
        ctest --test-dir "$BUILD" --output-on-failure -R "^($p: |ui: $p panel\$)"; then :; fi
    realtime_owners="${realtime_owners:+$realtime_owners|}$p"
done
if [ -n "$realtime_owners" ]; then
    if ! run_logged "real-time tests ($realtime_owners)" "$LOGS/ctest-realtime.log" \
        ctest --test-dir "$BUILD" --output-on-failure -R "^($realtime_owners): .*realtime and performance"; then :; fi
fi

VALIDATOR="$BUILD/bin/Release/validator"
PLUGINVAL="${PLUGINVAL:-$HOME/.cache/kraftonic/tools/pluginval/pluginval.app/Contents/MacOS/pluginval}"
for p in ${products[@]+"${products[@]}"}; do
    name="$(plugin_target "$p")"
    bundle="$BUILD/VST3/Release/$name.vst3"
    if [ ! -x "$VALIDATOR" ]; then fail "Steinberg validator missing: $VALIDATOR"; continue; fi
    if [ ! -d "$bundle" ]; then fail "$name bundle missing: $bundle"; continue; fi
    if run_logged "$name Steinberg validator" "$LOGS/validator-$p.log" "$VALIDATOR" "$bundle" && [ "$FULL" = 1 ]; then
        if [ ! -x "$PLUGINVAL" ]; then
            fail "pluginval missing: $PLUGINVAL (set PLUGINVAL=...)"
        else
            run_logged "$name pluginval strictness 10" "$LOGS/pluginval-$p.log" \
                "$PLUGINVAL" --strictness-level 10 --validate-in-process --timeout-ms 120000 \
                --vst3validator "$VALIDATOR" "$bundle" || :
        fi
    fi
done
# CLAP plug-ins: clap-validator on each (a pinned release, downloaded once; see the script).
claps=()
for p in ${products[@]+"${products[@]}"}; do
    claps+=("$BUILD/VST3/Release/$(plugin_target "$p").clap")
done
if [ ${#claps[@]} -gt 0 ]; then
    clap_out="$(scripts/ci/clap-validator.sh "$LOGS" "${claps[@]}" 2>&1)"
    clap_rc=$?
    while IFS= read -r line; do
        case "$line" in
            PASS:*) pass "${line#PASS: }" ;;
            FAIL:*) fail "${line#FAIL: }" ;;
            *) echo "$line" ;;
        esac
    done <<< "$clap_out"
    [ "$clap_rc" = 0 ] || [ "$FAILED" = 1 ] || fail "clap-validator (exit $clap_rc)"
fi
# Audio Units (macOS): auval on each, briefly installed in ~/Library (see the script).
if [ "$MACOS" = 1 ]; then
    components=()
    for p in ${products[@]+"${products[@]}"}; do
        component="$BUILD/VST3/Release/$(plugin_target "$p").component"
        if [ -d "$component" ]; then components+=("$component"); else fail "Audio Unit missing: $component"; fi
    done
    if [ ${#components[@]} -gt 0 ]; then
        auval_out="$(scripts/ci/auval.sh "$LOGS" "${components[@]}" 2>&1)"
        auval_rc=$?
        while IFS= read -r line; do
            case "$line" in
                PASS:*) pass "${line#PASS: }" ;;
                FAIL:*) fail "${line#FAIL: }" ;;
                *) echo "$line" ;;
            esac
        done <<< "$auval_out"
        [ "$auval_rc" = 0 ] || [ "$FAILED" = 1 ] || fail "auval (exit $auval_rc)"
    fi
fi
[ "$FULL" = 1 ] || say "pluginval skipped (scripts/check.sh $REQUESTED_PRODUCT --full runs it; CI always does)."
finish
