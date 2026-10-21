#!/bin/sh
# Installiert die Proxy-DLL: Original -> mss32_orig.dll, Proxy -> mss32.dll. Mehrfach ausfuehrbar.
set -e
cd "$(dirname "$0")"
BIN="../bin"
[ -f mss32.dll ] || ./build.sh
if [ ! -f "$BIN/mss32_orig.dll" ]; then
    mv "$BIN/mss32.dll" "$BIN/mss32_orig.dll"
fi
cp mss32.dll "$BIN/mss32.dll"
echo "installiert in $BIN (Log: $BIN/DiscordRPC.log)"
