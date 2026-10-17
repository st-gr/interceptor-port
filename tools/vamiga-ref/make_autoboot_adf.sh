#!/usr/bin/env bash
# tools/vamiga-ref/make_autoboot_adf.sh - copy of the game disk whose startup-sequence runs the game.
# AROS's shell splits "F-18 Interceptor" at the space even when quoted, and xdftool has no rename,
# so the executable is read out, deleted and written back as "game" (same size, so it fits).
set -euo pipefail
. /sandbox/.tools/env.sh
cd /sandbox/interceptor-wasm
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
cp extracted/interceptor.adf extracted/interceptor-autoboot.adf
xdftool extracted/interceptor-autoboot.adf read "F-18 Interceptor" "$tmp/game"
printf 'game\n' > "$tmp/startup-sequence"
xdftool extracted/interceptor-autoboot.adf delete "F-18 Interceptor" + delete s/startup-sequence \
    + write "$tmp/game" game + write "$tmp/startup-sequence" s/startup-sequence
