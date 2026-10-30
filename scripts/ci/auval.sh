#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
#
# Validate built Audio Units (*.component) with Apple's auval (macOS only).
#
# Usage: scripts/ci/auval.sh <logs-dir> <x.component>...
# Prints "PASS: <name> auval <type> <subtype> <manufacturer>" or "FAIL: ..."
# per component and writes <logs-dir>/auval-<name>.log. Exits 1 if any failed.
#
# auval only finds registered components, so each one is copied into
# ~/Library/Audio/Plug-Ins/Components for its test and removed again when
# this script exits (also on failure or Ctrl-C). Nothing else there is
# touched: if a component with the same file name is already installed, or
# another copy registers the same codes, the check fails instead of testing
# the wrong copy.
set -uo pipefail
[ $# -ge 2 ] || { echo "usage: $0 <logs-dir> <x.component>..." >&2; exit 2; }
LOGS="$1"
shift
DEST="$HOME/Library/Audio/Plug-Ins/Components"
mkdir -p "$DEST" "$LOGS"

installed=()
# shellcheck disable=SC2329 # called by the EXIT trap
cleanup() { for c in ${installed[@]+"${installed[@]}"}; do rm -rf "$c"; done; }
trap cleanup EXIT
trap 'exit 130' INT TERM

failed=0
for comp in "$@"; do
    name="$(basename "$comp" .component)"
    plist="$comp/Contents/Info.plist"
    ids=()
    for key in type subtype manufacturer; do
        ids+=("$(/usr/libexec/PlistBuddy -c "Print :AudioComponents:0:$key" "$plist" 2> /dev/null)")
    done
    codes="${ids[*]}"
    label="$name auval $codes"
    target="$DEST/$name.component"
    if [ -e "$target" ]; then
        echo "FAIL: $label ($target is already installed; remove it to check this build)"
        failed=1
        continue
    fi
    cp -R "$comp" "$target"
    installed+=("$target")

    # The component registry notices the new component within a few seconds.
    registered=""
    for _ in $(seq 1 30); do
        registered="$(auval -l 2> /dev/null | grep -F "$codes  -" || true)"
        [ -n "$registered" ] && break
        sleep 1
    done
    log="$LOGS/auval-$name.log"
    if [ -z "$registered" ]; then
        echo "FAIL: $label (not registered after 30 s)"
        failed=1
    elif [ "$(printf '%s\n' "$registered" | wc -l | tr -d ' ')" != 1 ] \
        || ! printf '%s\n' "$registered" | grep -qF "file://${target// /%20}/"; then
        echo "FAIL: $label (another component registers these codes: $registered)"
        failed=1
    elif auval -v "${ids[@]}" > "$log" 2>&1; then
        echo "PASS: $label"
    else
        echo "FAIL: $label (see $log)"
        grep -E "ERROR|FAIL" "$log" | head -10
        failed=1
    fi
    rm -rf "$target"
done
exit "$failed"
