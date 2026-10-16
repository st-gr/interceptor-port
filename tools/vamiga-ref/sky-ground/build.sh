#!/bin/bash
# Build librigblit.so (the rig with the blitter model) next to the Musashi sources of tools/m68k-rig; output: build/sky-ground/librigblit.so
set -e
cd "$(dirname "$0")/../../.."
R=tools/m68k-rig
M=$R/musashi
if [ ! -f $M/m68kops.c ]; then (cd $M && gcc -o m68kmake m68kmake.c && ./m68kmake); fi
mkdir -p build/sky-ground
gcc -O2 -fPIC -shared -DMUSASHI_CNF="\"$PWD/$R/m68kconf.h\"" -I$R -I$M \
    tools/vamiga-ref/sky-ground/rigblit.c $M/m68kcpu.c $M/m68kops.c $M/m68kdasm.c $M/softfloat/softfloat.c \
    -o build/sky-ground/librigblit.so -lm
echo built build/sky-ground/librigblit.so
