# CS:GO Supremacy – Discord Rich Presence

A small in-process plugin that shows your CS:GO Legacy (Supremacy) status on Discord:

| State | Shown |
|-------|-------|
| Main menu | "In Main Menu" + project logo |
| Inventory | "Browsing Inventory" |
| Inspecting an item | `Inspecting: <item name>` and `<condition> - Float <value>` (items without a float show their type) |
| Viewing / opening a case | "Viewing Case" / "Opening Case" with the case name |
| Unboxed item | `Unboxed: <item name>` with condition and float |
| Loading | "Loading map" + map name |
| In a match | `<Game mode> - <map>`, score (`CT 7 : 5 T`), match timer, map screenshot as the large image, your team as the small image |
| Spectating | same as above, the small image is an eye icon ("Spectating") |

Everything is read directly from the game process using the public 2023 Windows CS:GO offsets
([hazedumper](https://github.com/frk1/hazedumper)); team scores and game mode come from the game's own netvar and ConVar tables.

Menu, inventory, item and case states come from Panorama (the game's UI): the DLL runs a small script (`bridge.js`) in the main menu
context through `CUIEngine::RunScript`; the script reports back through the console log (`csgo/rpc_console.log`).
Hovering over an item (tooltip) is not detected, only the inspect window. "Opening Case" and "Unboxed" are based on the game's UI code
and have not been tested with a real case.

**Platform:** Linux (Proton/Wine) and Windows. The plugin connects to Discord through whatever IPC channel it finds, in this order:
1. Windows named pipe `\\?\pipe\discord-ipc-0..9` (Discord on Windows, arRPC/Vesktop on Windows, `wine-discord-ipc-bridge` under Wine)
2. Unix socket `discord-ipc-0..9` in `XDG_RUNTIME_DIR`, `TMPDIR`, `TMP`, `TEMP`, `/run/user/<uid>` and `/tmp`, including the Flatpak
   (Discord, Canary, Vesktop, WebCord) and Snap (Discord, Canary) sub-folders. Under Wine it reaches them through the `Z:` drive.

The Linux/Proton path with the native Discord client is the tested one; the Windows named-pipe path has not been tested yet.

## Install

1. Download the latest release and extract `bin/` into your Supremacy game folder, so that you end up with
   `<game folder>/bin/mss32.dll` and `<game folder>/bin/mss32_orig.dll`.
   - `mss32_orig.dll` is the unmodified Miles Sound System DLL that ships with the game.
   - `mss32.dll` is the proxy: it forwards all 393 exports to `mss32_orig.dll` and starts the Rich Presence thread.
   - If you already have the game's own `mss32.dll`, don't overwrite it before renaming it to `mss32_orig.dll`.
2. Start the game through the Supremacy launcher as usual. The DLL loads automatically, no launch options needed.
3. Make sure Discord is running.

To uninstall, delete `mss32.dll` and rename `mss32_orig.dll` back to `mss32.dll`.

## Troubleshooting

To switch the Panorama part off (menu/inventory states), create an empty file `<game folder>/bin/rpc_nopanorama.txt`.
For development, `bin/rpc_script.js` replaces the built-in script and is re-run whenever it changes (copy `bridge.js` there to start).

The plugin writes a log to `<game folder>/bin/DiscordRPC.log` (connection, game state, every presence update).

- *No presence at all*: look for `discord: connected via` in the log. If it is missing, Discord's pipe/socket is not reachable
  (under Proton, a Flatpak/Snap Discord may need its socket exposed to the container).
- *No images*: images are loaded from this repository (`assets/`) through direct URLs. Check the log for the `SET_ACTIVITY` line.
- *No score*: the log shows the netvar/team scan result. An offset that doesn't match your game build makes it fall back to "In game".
- If the launcher verifies game files and restores `mss32.dll`, install it again.

## Build from source

Requires `i686-w64-mingw32-g++` (Arch: `mingw-w64-gcc`), Python 3, ImageMagick and `rsvg-convert` (assets only).

```sh
git clone https://github.com/wildesPepega/csgo-rpc-assets.git DiscordRPC   # into <game folder>/DiscordRPC
cd DiscordRPC
./build.sh      # generates mss32.def from the original DLL and builds mss32.dll
./install.sh    # mss32.dll -> mss32_orig.dll, proxy -> bin/mss32.dll
./uninstall.sh
```

Configuration lives in `discord_rpc.cpp`: `kClientId` (Discord application ID), `kAssetBaseUrl` (image host) and the `off` namespace (offsets).
`gen_assets.py` regenerates `assets/` (map screenshots from the game files, team icons from `icons_src/`, spectator eye)
and `assets_keys.h`.

## Notes

Map screenshots and the logo come from the game / launcher files and belong to their respective owners. The repository ships the original
`mss32.dll` (RAD Game Tools / Miles Sound System) in the release only for convenience.
