# CS:GO Supremacy – Discord Rich Presence

A small in-process plugin that shows your CS:GO Legacy (Supremacy) status on Discord:

| State | Shown |
|-------|-------|
| Main menu | "In Main Menu" + project logo |
| Loading | "Loading map" + map name |
| In a match | `<Game mode> - <map>`, score (`CT 7 : 5 T`), match timer, map screenshot as the large image, your team as the small image |
| Spectating | same as above, the small image is an eye icon ("Spectating") |

Everything is read directly from the game process using the public 2023 Windows CS:GO offsets
([hazedumper](https://github.com/frk1/hazedumper)); team scores and game mode come from the game's own netvar and ConVar tables.
Inventory detection is not implemented (the inventory shows as "In Main Menu").

**Platform:** Linux (Proton/Wine) with the native Discord client. It talks to Discord's IPC socket
(`$XDG_RUNTIME_DIR/discord-ipc-0`) through Wine's AF_UNIX support. Windows is not supported.

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

The plugin writes a log to `<game folder>/bin/DiscordRPC.log` (connection, game state, every presence update).

- *No presence at all*: look for `discord: verbunden` in the log. If it is missing, Discord's socket is not reachable from the
  Proton container (Flatpak/Snap Discord may need its socket exposed).
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
