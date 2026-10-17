#!/usr/bin/env bash
# tools/vamiga-ref/build.sh - build vAmiga Core headless plus our driver. Linux sandbox only.
# The (slow, ~15 min) CMake tree lives outside the repo so that "make clean" in the repo does not
# throw it away; only the finished driver is installed to build/vamiga-ref/ref.
set -euo pipefail
. /sandbox/.tools/env.sh
micromamba install -y -p /sandbox/.tools/env -c conda-forge cmake zlib >/dev/null
VAMIGA_SHA=899da5ddd872dfb2e9e35e80247cbb2b36086524   # pinned upstream commit
SRC=/sandbox/vamiga
[ -d "$SRC/.git" ] || git clone https://github.com/dirkwhoffmann/vAmiga "$SRC"
git -C "$SRC" cat-file -e "$VAMIGA_SHA^{commit}" 2>/dev/null || git -C "$SRC" fetch origin "$VAMIGA_SHA"
git -C "$SRC" checkout -q "$VAMIGA_SHA"
REPO=/sandbox/interceptor-wasm
TREE=/sandbox/vamiga-ref-build
OUT="$REPO/build/vamiga-ref"
# The conda toolchain ships ar/ranlib only under prefixed names, which CMake does not find.
cmake -S "$REPO/tools/vamiga-ref" -B "$TREE" -DCMAKE_BUILD_TYPE=Release -DVAMIGA_SRC="$SRC" \
    -DCMAKE_AR=/sandbox/.tools/env/bin/x86_64-conda-linux-gnu-ar \
    -DCMAKE_RANLIB=/sandbox/.tools/env/bin/x86_64-conda-linux-gnu-ranlib >/dev/null
cmake --build "$TREE" -j"$(nproc)" --target ref
mkdir -p "$OUT"
cp "$TREE/ref" "$OUT/ref"
echo "built $OUT/ref"
