#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
#
# Print a short per-product CI summary (Markdown) from the logs of one CI job:
#   <logs-dir>/ctest.log        tests per owner prefix ("core:", "startersynth:", ...)
#   <logs-dir>/validation.tsv   from scripts/ci/validate-plugins.sh
#   <installer-dir>/*           installer files, if any
# Usage: scripts/ci/summarize.sh <logs-dir> [installer-dir] [title]
# Also appends to $GITHUB_STEP_SUMMARY when set. Never fails the job.
set -uo pipefail
LOGS="$1"
INSTALLERS="${2:-}"
TITLE="${3:-$(uname -s)}"

{
    echo "### $TITLE"
    echo
    if [ -f "$LOGS/ctest.log" ]; then
        echo "| Tests (owner) | Passed | Failed |"
        echo "|---|---:|---:|"
        # ctest lines look like: "  3/42 Test  #3: startersynth: voice decays ....   Passed  0.01 sec"
        awk '
            /^ *[0-9]+\/[0-9]+ +Test +#[0-9]+: / {
                line = $0
                sub(/^ *[0-9]+\/[0-9]+ +Test +#[0-9]+: /, "", line)
                owner = "(no prefix)"
                if (match(line, /^[a-z0-9-]+: /)) owner = substr(line, 1, RLENGTH - 2)
                if (owners[owner] == "") { order[++n] = owner; owners[owner] = 1 }
                if (line ~ / Passed +[0-9.]+ sec *$/) pass[owner]++; else fail[owner]++
            }
            END {
                for (i = 1; i <= n; i++) printf "| %s | %d | %d |\n", order[i], pass[order[i]], fail[order[i]]
                if (n == 0) print "| (no tests found in ctest.log) | | |"
            }' "$LOGS/ctest.log"
        grep -E '^[0-9]+% tests passed' "$LOGS/ctest.log" | tail -1 | sed 's/^/\n/'
        echo
    fi
    if [ -f "$LOGS/validation.tsv" ] && [ "$(wc -l < "$LOGS/validation.tsv")" -gt 1 ]; then
        echo "| Product | Bundle | Steinberg validator | pluginval |"
        echo "|---|---|---|---|"
        tail -n +2 "$LOGS/validation.tsv" | awk -F '\t' '{ printf "| %s | `%s` | %s | %s |\n", $1, $2, $3, $4 }'
        echo
    else
        echo "No plug-in bundles were built or validated."
        echo
    fi
    if [ -n "$INSTALLERS" ] && [ -d "$INSTALLERS" ]; then
        for f in "$INSTALLERS"/*; do
            [ -f "$f" ] || continue
            size=$(( $(wc -c < "$f") / 1024 ))
            echo "Installer: \`$(basename "$f")\` (${size} KiB)"
        done
        echo
    fi
} | tee -a "${GITHUB_STEP_SUMMARY:-/dev/null}"
exit 0
