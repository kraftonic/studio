#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
#
# Validate built CLAP plug-ins (*.clap) with clap-validator.
#
# Usage: scripts/ci/clap-validator.sh <logs-dir> <x.clap>...
# Prints "PASS: <name> clap-validator (<summary>)" or "FAIL: ..." per plug-in
# and writes <logs-dir>/clap-validator-<name>.log. Exits 1 if any failed.
#
# clap-validator (MIT, https://github.com/free-audio/clap-validator) is a
# pinned prebuilt release, checked against its SHA-256 and cached in
# ~/.cache/kraftonic/tools (KRAFTONIC_TOOLS overrides the folder;
# CLAP_VALIDATOR=<path> uses another binary).
set -uo pipefail
[ $# -ge 2 ] || { echo "usage: $0 <logs-dir> <x.clap>..." >&2; exit 2; }
LOGS="$1"
shift
mkdir -p "$LOGS"

VERSION=0.4.1
BUILD=0.4.1-127-g152b982
case "$(uname -s)" in
    Darwin) asset=macos-universal; sha=bbec8cd7d18274e549d5d8c12ece3cec54be966129388dd2e742b9957f2ba9f1 ;;
    Linux) asset=ubuntu-22.04; sha=49edadcfb407ea0dd946ce418300e853fbd2660fa4b0d00e4f19ff8eef24ad90 ;;
    MINGW* | MSYS* | CYGWIN*) asset=windows; sha=d935c3af0a45c3911ea2e900f4aa5d6709dac82bb485f0c4ce28648ab2cd0c10 ;;
    *) echo "FAIL: clap-validator (no prebuilt release for $(uname -s))"; exit 1 ;;
esac

sha256() {
    if command -v sha256sum > /dev/null; then sha256sum "$1" | cut -d' ' -f1; else shasum -a 256 "$1" | cut -d' ' -f1; fi
}

VALIDATOR="${CLAP_VALIDATOR:-}"
if [ -z "$VALIDATOR" ]; then
    dir="${KRAFTONIC_TOOLS:-$HOME/.cache/kraftonic/tools}/clap-validator-$VERSION"
    VALIDATOR="$dir/clap-validator"
    [ "$asset" = windows ] && VALIDATOR="$dir/clap-validator.exe"
    if [ ! -x "$VALIDATOR" ]; then
        mkdir -p "$dir"
        zip="$dir/download.zip"
        url="https://github.com/free-audio/clap-validator/releases/download/$VERSION/clap-validator-$BUILD-$asset.zip"
        if ! curl -fsSL -o "$zip" "$url"; then
            echo "FAIL: clap-validator download ($url)"
            exit 1
        fi
        if [ "$(sha256 "$zip")" != "$sha" ]; then
            rm -f "$zip"
            echo "FAIL: clap-validator download has the wrong SHA-256 ($url)"
            exit 1
        fi
        unzip -q -o "$zip" -d "$dir/unpacked" || { echo "FAIL: clap-validator unzip"; exit 1; }
        if [ "$asset" = windows ]; then
            mv "$dir/unpacked/clap-validator.exe" "$VALIDATOR"
        else
            # The zip holds a tarball with clap-validator at its top level (Linux)
            # or under binaries/ (macOS).
            tar -xzf "$dir"/unpacked/*.tar.gz -C "$dir/unpacked" || { echo "FAIL: clap-validator untar"; exit 1; }
            found="$(find "$dir/unpacked" -type f -name clap-validator | head -n 1)"
            [ -n "$found" ] || { echo "FAIL: clap-validator not found in the download"; exit 1; }
            mv "$found" "$VALIDATOR"
        fi
        rm -rf "$zip" "$dir/unpacked"
        chmod +x "$VALIDATOR"
    fi
fi

failed=0
for plugin in "$@"; do
    name="$(basename "$plugin" .clap)"
    log="$LOGS/clap-validator-$name.log"
    if [ ! -e "$plugin" ]; then
        echo "FAIL: $name clap-validator (missing: $plugin)"
        failed=1
        continue
    fi
    NO_COLOR=1 "$VALIDATOR" validate "$plugin" > "$log" 2>&1
    rc=$?
    # "44 tests run, 35 passed, 0 failed, 0 warnings, 9 skipped"
    summary="$(sed 's/\x1b\[[0-9;]*m//g' "$log" | grep -Eo '[0-9]+ tests run, [0-9]+ passed, [0-9]+ failed, [0-9]+ warnings?, [0-9]+ skipped' | tail -1)"
    if [ $rc -eq 0 ] && [[ "$summary" == *", 0 failed,"* ]]; then
        echo "PASS: $name clap-validator ($summary)"
    else
        echo "FAIL: $name clap-validator (${summary:-exit $rc}; see $log)"
        sed 's/\x1b\[[0-9;]*m//g' "$log" | grep -B6 -A6 "FAILED" | head -40
        failed=1
    fi
done
exit "$failed"
