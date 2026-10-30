#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
#
# Remove everything the Kraftonic Studio macOS installer put on this Mac.
#
# Usage: sudo packaging/macos/uninstall.sh [--dry-run] [--root <dir>]
#   --dry-run     print what would be removed, remove nothing
#   --root <dir>  act on <dir> instead of / (for testing an expanded payload);
#                 installer receipts are only forgotten when acting on /
#
# Removes /Library/Audio/Plug-Ins/VST3/Kraftonic*.vst3,
# /Library/Audio/Plug-Ins/Components/Kraftonic*.component,
# /Library/Audio/Plug-Ins/CLAP/Kraftonic*.clap and
# /Library/Application Support/Kraftonic, then forgets the com.kraftonic.studio.* receipts.
set -euo pipefail
DRY=0 ROOT=""
while [ $# -gt 0 ]; do
    case "$1" in
        --dry-run) DRY=1; shift ;;
        --root) [ $# -ge 2 ] || { echo "error: --root needs a folder" >&2; exit 1; }; ROOT="${2%/}"; shift 2 ;;
        -h|--help) sed -n '4,14p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
        *) echo "error: unknown option: $1 (try --help)" >&2; exit 1 ;;
    esac
done
run() { if [ "$DRY" = 1 ]; then echo "would run: $*"; else echo "$*"; "$@"; fi; }

shopt -s nullglob
for b in "$ROOT/Library/Audio/Plug-Ins/VST3/"Kraftonic*.vst3 "$ROOT/Library/Audio/Plug-Ins/Components/"Kraftonic*.component \
    "$ROOT/Library/Audio/Plug-Ins/CLAP/"Kraftonic*.clap; do
    run rm -rf "$b"
done
[ -d "$ROOT/Library/Application Support/Kraftonic" ] && run rm -rf "$ROOT/Library/Application Support/Kraftonic"
if [ -z "$ROOT" ]; then
    for r in $(pkgutil --pkgs='com\.kraftonic\.studio\..*' 2> /dev/null); do run pkgutil --forget "$r"; done
fi
echo "Done. Rescan plug-ins in your host."
