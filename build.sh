#!/bin/sh
# Baut die Proxy-DLL (32-bit) mit MinGW. Aufruf: ./build.sh
set -e
cd "$(dirname "$0")"
ORIG="../bin/mss32_orig.dll"
[ -f "$ORIG" ] || ORIG="../bin/mss32.dll"
python3 -I gen_def.py "$ORIG" mss32.def
i686-w64-mingw32-g++ -std=c++17 -O2 -Wall -shared -static -static-libgcc -static-libstdc++ \
    -o mss32.dll discord_rpc.cpp mss32.def -lws2_32
echo "gebaut: $(pwd)/mss32.dll"
