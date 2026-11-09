#!/bin/sh
# Builds res/bcvis.dll with MinGW-w64; MinHook is linked statically.
set -eu
cd "$(dirname "$0")/.."
M=vendor/minhook
CC="${CC:-x86_64-w64-mingw32-gcc}"
CXX="${CXX:-x86_64-w64-mingw32-g++}"
OUT=$(mktemp -d)
trap 'rm -rf "$OUT"' EXIT HUP INT TERM
for c in "$M/src/buffer.c" "$M/src/hook.c" "$M/src/trampoline.c" "$M/src/hde/hde64.c"; do
    "$CC" -O2 -c "$c" -I"$M/include" -o "$OUT/$(basename "$c" .c).o"
done
"$CXX" -std=c++20 -O2 -c vistrace/VisTrace.cpp -I"$M/include" -o "$OUT/VisTrace.o"
"$CXX" -shared -static -s -O2 -o res/bcvis.dll "$OUT"/*.o -Wl,--subsystem,windows
echo "res/bcvis.dll built"
