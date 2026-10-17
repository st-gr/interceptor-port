#!/usr/bin/env bash
# tools/setup-toolchain.sh - everything the sandbox needs beyond openshell/setup-rea.sh. Safe to re-run.
set -euo pipefail
. /sandbox/.tools/env.sh
micromamba install -y -p /sandbox/.tools/env -c conda-forge gcc gxx make pip sdl2=2.32.56 pkg-config cmake zlib emscripten=4.0.9 pytest
# conda ships only a prefixed binutils ar; the Makefile calls plain `ar`.
if ! command -v ar >/dev/null 2>&1; then
  ln -sf /sandbox/.tools/env/bin/x86_64-conda-linux-gnu-ar /sandbox/.tools/bin/ar
fi
/sandbox/.tools/env/bin/pip install -q "git+https://github.com/keirf/greaseweazle@latest" amitools
/sandbox/.tools/env/bin/pip install -q -e /sandbox/interceptor-wasm/tools/extract
# Emscripten 4.0.9 from conda-forge expects binaryen 123, but conda-forge only has 117, whose
# wasm-opt rejects emcc's flags. Use the upstream release and point emcc at it.
BINARYEN_SHA256=e959f2170af4c20c552e9de3a0253704d6a9d2766e8fdb88e4d6ac4bae9388fe  # version_123
if [ ! -x /sandbox/.tools/binaryen/bin/wasm-opt ]; then
  rm -rf /tmp/binaryen.tgz /tmp/binaryen.new /sandbox/.tools/binaryen
  curl -fSL --retry 3 -o /tmp/binaryen.tgz https://github.com/WebAssembly/binaryen/releases/download/version_123/binaryen-version_123-x86_64-linux.tar.gz
  echo "$BINARYEN_SHA256  /tmp/binaryen.tgz" | sha256sum -c -
  mkdir /tmp/binaryen.new && tar xzf /tmp/binaryen.tgz -C /tmp/binaryen.new --strip-components=1
  mv /tmp/binaryen.new /sandbox/.tools/binaryen
  rm -f /tmp/binaryen.tgz
fi
grep -q EM_BINARYEN_ROOT /sandbox/.tools/env.sh || \
  echo 'export EM_BINARYEN_ROOT="/sandbox/.tools/binaryen"' >> /sandbox/.tools/env.sh
export EM_BINARYEN_ROOT=/sandbox/.tools/binaryen

# tcc: the 0.9.27 release segfaults when linking against glibc 2.39 (tested here), so build
# the TinyCC mob head pinned to a known-good commit instead.
TCC_REV=43c7708b85681a2fd4451c8a541af4494a8919b2
if [ ! -x /sandbox/.tools/tcc/bin/tcc ]; then
  rm -rf /tmp/tinycc /sandbox/.tools/tcc
  git clone -q --filter=blob:none https://github.com/TinyCC/tinycc /tmp/tinycc
  git -C /tmp/tinycc checkout -q "$TCC_REV"
  # There is no system libc in /usr here, only the conda sysroot. Its libc.so/libm.so are ld
  # scripts with absolute /lib64 paths that tcc cannot resolve, so give tcc its own lib dir.
  SR=/sandbox/.tools/env/x86_64-conda-linux-gnu/sysroot
  SL=/sandbox/.tools/tcc/syslib
  mkdir -p "$SL"
  printf 'GROUP ( %s %s )\n' "$SR/lib64/libc.so.6" "$SR/usr/lib64/libc_nonshared.a" > "$SL/libc.so"
  ln -sf "$SR/lib64/libm.so.6" "$SL/libm.so"
  # bcheck.o is skipped (BCHECK_O=): it needs glibc malloc hooks that glibc 2.39 lacks.
  (cd /tmp/tinycc && ./configure --prefix=/sandbox/.tools/tcc \
     --sysincludepaths="{B}/include:$SR/usr/include:/sandbox/.tools/env/include" \
     --libpaths="$SL:$SR/lib64:$SR/usr/lib64:/sandbox/.tools/env/lib" \
     --crtprefix="$SR/usr/lib64" >/dev/null \
   && make -j"$(nproc)" BCHECK_O= >/dev/null && make install BCHECK_O= >/dev/null)
  ln -sf /sandbox/.tools/tcc/lib/tcc/libtcc1.a "$SL/libtcc1.a"   # tcc looks for it in the first lib path
fi
grep -q '/sandbox/.tools/tcc/bin' /sandbox/.tools/env.sh || \
  sed -i 's|^export PATH="|export PATH="/sandbox/.tools/tcc/bin:|' /sandbox/.tools/env.sh
# rea picks the Ghidra language from this variable; the rea MCP server inherits it from the shell
# that starts Claude Code (see tools/hunk/ghidra_import.md).
grep -q REA_GHIDRA_LANGUAGE_ID /sandbox/.tools/env.sh || \
  echo 'export REA_GHIDRA_LANGUAGE_ID=68000:BE:32:default' >> /sandbox/.tools/env.sh
echo "toolchain ready: $(gcc --version | head -1); $(/sandbox/.tools/tcc/bin/tcc -v); $(emcc --version | head -1)"
