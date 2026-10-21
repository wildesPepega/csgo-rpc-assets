#!/bin/sh
# Stellt das Original-mss32.dll wieder her.
set -e
cd "$(dirname "$0")"
BIN="../bin"
[ -f "$BIN/mss32_orig.dll" ] || { echo "nichts zu tun"; exit 0; }
mv -f "$BIN/mss32_orig.dll" "$BIN/mss32.dll"
rm -f "$BIN/DiscordRPC.log"
echo "Original wiederhergestellt"
