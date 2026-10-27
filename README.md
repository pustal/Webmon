# Webmon

<img src="webos/largeIcon.png" alt="App icon" width="96" align="right">

Runs [gen1recomp](https://github.com/bryanthaboi/gen1recomp) (G1R Deluxe) on
LG webOS TVs. gen1recomp is a LÖVE remake of the Game Boy Pokemon games whose
data and graphics are read from your own ROM; Webmon is a native LÖVE 11.5 build
for the TV plus webOS integration: an upload page, USB storage and a file
picker for the game's import screens.

> Based on the Pokemon Gen 1 Recompilation Project by BOIS CLUB GAMES, LLC
> (https://github.com/bryanthaboi/gen1recomp)

Webmon contains no gen1recomp code and no game data. gen1recomp's launcher may
only be distributed in unmodified official releases, so Webmon gets the
official release's `.love` for you (or you add it yourself), and you supply
your own ROM.

## Installing

1. Install `dist/org.webmon.app_<version>_arm.ipk` with
   [webOS Dev Manager](https://github.com/webosbrew/dev-manager-desktop) or
   `ares-install`.
2. Start Webmon. Without the game it shows a setup screen:
   - press **OK** to download the latest gen1recomp release straight onto the
     TV (it restarts into the game when done), or
   - open the upload page address shown there on a phone or computer on the
     same network, enter the PIN and upload or download the `.love`, or
   - copy the `.love` into a folder named `Webmon` on a USB drive.
3. In the game, **Import ROM** opens Webmon's file picker on the TV. It lists
   the ROMs you uploaded, the ones in the USB `Webmon/roms` folder, and the ones
   in RetroArch-style `roms/` folders on any USB drive. Saves work the same way
   (`Webmon/saves`, RetroArch `saves/`, `.sav` or `.srm`), as do mod zips and
   carts.

Press **1** on the remote at any time to show or hide the upload page address
and PIN (it also shows for a few seconds at start).

## The upload page

Port 47811 (or the next free one). It only answers devices on your home
network that send the PIN. It shows:

- **TV**: free space, root access (the app running as root, or root commands
  through the Homebrew Channel), whether the TV can download by itself (curl),
  and which USB drive Webmon uses;
- **USB drives**: free space and access, and a button to create a `Webmon`
  folder on a drive;
- **The game**: download the latest release, or any listed release, onto the
  TV, or download it to your computer;
- **Files**: upload, list and delete the game, ROMs, saves, mod zips and carts,
  to TV storage or the USB `Webmon` folder. While the game's picker is open the
  page says what it is waiting for, and new uploads appear in the picker by
  themselves.

The game's own downloads (mod catalogue, updates) also use the TV's curl.

## Where files go

| Place | Contents |
|---|---|
| `<USB>/Webmon/` | the `.love`; `roms/`, `saves/`, `mod-zips/`, `carts/` for the game's import screens; `mods/`, the game's installed mods |
| `<USB>/roms/`, `<USB>/saves/` | RetroArch-style folders (any capitalisation, up to three levels deep): listed by the picker, never changed |
| Data folder `game/`, `inbox/` | the same, on TV storage, when there is no USB `Webmon` folder (or you choose TV storage) |
| Data folder `save/love/pokemon-love2d/` | the game's own saves and settings; its `mods/` links to the USB drive when the drive's `Webmon` folder can be written |

When the app cannot read a USB drive itself, Webmon uses root (if available) to
bind-mount the `Webmon` folder into the data folder and to copy picked files.

The data folder is
`/media/developer/apps/usr/palm/applications/org.webmon.app/data`, with
`stdout.log` and `stderr.log`. It survives app updates but not uninstalling.

## Controls

The remote's Back and Exit keys act as Escape in the game. Arrows and OK work
as the keyboard's arrows and Enter; 1 shows the info panel. A game controller
works as in gen1recomp. In Webmon's picker: Up/Down move, Left/Right page,
OK (A) chooses, Back (B) cancels.

## Building

You need Linux x86_64 (or a Linux VM/container on macOS), the webosbrew
toolchain ([buildroot-nc4 releases](https://github.com/openlgtv/buildroot-nc4/releases),
run its `relocate-sdk.sh` once), CMake 3.21+, Ninja or make, autoconf,
automake, libtool, pkg-config, git, a 32-bit host compiler (`gcc-multilib`, for
LuaJIT's build tools) and `ares-package` (`npm install -g @webos-tools/cli`).

```sh
export WEBOS_SDK=$HOME/arm-webos-linux-gnueabi_sdk-buildroot
scripts/build-webos.sh
```

`scripts/build-deps.sh` builds LÖVE 11.5 and its libraries from pinned sources
(LuaJIT at the commit gen1recomp pins, OpenAL Soft with SDL2 output, FreeType,
Ogg/Vorbis/Theora, modplug) into `third_party/deps-work/`. The IPK lands in
`dist/`.

Tests that run without a TV:

- `tools/server-harness/run.sh <scratch folder>` runs the upload page with the
  host compiler (open `http://localhost:47811`);
- `lua5.1 tools/bridge-test/test.lua src/lua/bridge.lua` checks the picker.

## How it works

| Part | What it does |
|---|---|
| `src/main.cpp` | LÖVE's own `main` (from `src/love.cpp`) plus the webOS start-up; runs the newest `.love` or the setup screen, and again after the game asks LÖVE to restart |
| `src/webos/Platform.cpp` | Data folder, logs, LÖVE's save folder (`XDG_DATA_HOME`), Back/Exit keys to Escape |
| `src/webos/Usb.cpp` | Root detection (Homebrew Channel exec), USB drives, the `Webmon` folder |
| `src/webos/Library.cpp` | Where files live, linking the USB `Webmon` folder and the game's `mods`, file lists including RetroArch folders |
| `src/webos/Downloader.cpp` | Downloads gen1recomp releases with the TV's curl |
| `src/server/WebServer.cpp`, `web/index.html` | The upload page (cpp-httplib) |
| `src/webos/LuaBridge.cpp`, `src/lua/bridge.lua` | Lua API for the game and setup screen; the file picker gen1recomp asks for (`love.system.pickFile` / `getPickedFile`) and the info panel |
| `setup/` | The setup screen (Lua) |

## Licence

MIT (see [LICENSE](LICENSE)) for Webmon's own code. See
[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) for the bundled libraries.
Pokemon and all related names are the property of Nintendo, Creatures Inc. and
GAME FREAK inc. Webmon is not affiliated with them or with BOIS CLUB GAMES.
