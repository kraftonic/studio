#!/bin/sh
# SPDX-License-Identifier: MIT
#
# Install the Kraftonic Studio plug-ins in this folder (VST3 and CLAP).
#   ./install.sh              -> ~/.vst3 and ~/.clap            (current user)
#   sudo ./install.sh --system -> /usr/lib/vst3 and /usr/lib/clap (all users)
#   ./install.sh --uninstall [--system]   removes them again
set -eu

HERE="$(cd "$(dirname "$0")" && pwd)"
DEST="${HOME}/.vst3"
CLAP_DEST="${HOME}/.clap"
MODE=install
for arg in "$@"; do
    case "$arg" in
        --system) DEST=/usr/lib/vst3; CLAP_DEST=/usr/lib/clap ;;
        --uninstall) MODE=uninstall ;;
        -h|--help) sed -n '4,7p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
        *) echo "unknown option: $arg (try --help)" >&2; exit 2 ;;
    esac
done

found=0
for bundle in "$HERE"/*.vst3 "$HERE"/*.clap; do
    [ -e "$bundle" ] || continue
    found=1
    name="$(basename "$bundle")"
    dest="$DEST"
    case "$name" in *.clap) dest="$CLAP_DEST" ;; esac
    if [ "$MODE" = uninstall ]; then
        if [ -e "$dest/$name" ]; then rm -rf "${dest:?}/${name:?}"; echo "Removed $dest/$name"; fi
        continue
    fi
    mkdir -p "$dest"
    # Replace any earlier version completely, so no stale files remain.
    rm -rf "$dest/$name.tmp"
    cp -R "$bundle" "$dest/$name.tmp"
    rm -rf "${dest:?}/${name:?}"
    mv "$dest/$name.tmp" "$dest/$name"
    echo "Installed $dest/$name"
done
[ "$found" = 1 ] || { echo "no .vst3 bundles next to $0" >&2; exit 1; }
[ "$MODE" = install ] && echo "Done. Rescan plug-ins in your host."
exit 0
