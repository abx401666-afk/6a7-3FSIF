#!/bin/sh
# Builds res/bcvis.dll with MinGW-w64 (x86_64-w64-mingw32), MinHook linked in statically.
set -e
cd "$(dirname "$0")/.."
M=vendor/minhook
OUT=$(mktemp -d)
for c in $M/src/buffer.c $M/src/hook.c $M/src/trampoline.c $M/src/hde/hde64.c; do
    x86_64-w64-mingw32-gcc -O2 -c "$c" -I$M/include -o "$OUT/$(basename "$c" .c).o"
done
x86_64-w64-mingw32-g++ -std=c++20 -O2 -c vistrace/VisTrace.cpp -I$M/include -o "$OUT/VisTrace.o"
x86_64-w64-mingw32-g++ -shared -static -s -O2 -o res/bcvis.dll "$OUT"/*.o -Wl,--subsystem,windows
rm -rf "$OUT"
echo "res/bcvis.dll built"
