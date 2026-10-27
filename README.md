# Webmon

<img src="webos/largeIcon.png" alt="App icon" width="96" align="right">

Runs [gen1recomp](https://github.com/bryanthaboi/gen1recomp) (G1R Deluxe) on
LG webOS TVs. gen1recomp is a LÖVE remake of the Game Boy Pokemon games whose
data and graphics are read from your own ROM; Webmon is a native LÖVE 11.5 build
for the TV plus webOS integration.

> Based on the Pokemon Gen 1 Recompilation Project by BOIS CLUB GAMES, LLC
> (https://github.com/bryanthaboi/gen1recomp)

Webmon contains no gen1recomp code and no game data. gen1recomp's launcher may
only be distributed in unmodified official releases, so you add the official
release's `.love` file yourself, plus your own ROM.

**Status: early.** This version checks that LÖVE runs on the TV and starts the
game from a `.love` you copy over. The upload page, USB support and downloads
come next.

## Installing (current version)

1. Install `dist/org.webmon.app_<version>_arm.ipk` with
   [webOS Dev Manager](https://github.com/webosbrew/dev-manager-desktop) or
   `ares-install`.
2. Get the `.love` file from gen1recomp's
   [releases](https://github.com/bryanthaboi/gen1recomp/releases) and put it in
   either:
   - a folder named `Webmon` on a USB drive, or
   - `/media/developer/apps/usr/palm/applications/org.webmon.app/data/game/`
3. Start Webmon. Without a `.love` it shows a setup screen with a LÖVE check
   (renderer, frame rate).

Logs: `stdout.log` and `stderr.log` in the data folder
(`/media/developer/apps/usr/palm/applications/org.webmon.app/data`). Saves are
in its `save/` folder.

## Controls

The remote's Back and Exit keys act as Escape in the game. Arrows and OK work
as the keyboard's arrows and Enter. A game controller works as in gen1recomp.

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

## How it works

| Part | What it does |
|---|---|
| `src/main.cpp` | LÖVE's own `main` (from `src/love.cpp`) plus the webOS start-up; runs the `.love` or the setup screen, and again after the game asks LÖVE to restart |
| `src/webos/Platform.cpp` | Data folder, logs, LÖVE's save folder (`XDG_DATA_HOME`), Back/Exit keys to Escape |
| `src/webos/GameLocator.cpp` | Finds the `.love` in the data folder or on USB drives |
| `setup/` | The setup screen (Lua) |

## Licence

MIT (see [LICENSE](LICENSE)) for Webmon's own code. See
[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) for the bundled libraries.
Pokemon and all related names are the property of Nintendo, Creatures Inc. and
GAME FREAK inc. Webmon is not affiliated with them or with BOIS CLUB GAMES.
