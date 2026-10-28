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
   the ROMs you uploaded, the ones in the USB `Webmon/roms` folder, and the
   Pokemon ones in RetroArch-style `roms/` folders on any USB drive (file names
   with "Pokemon", "Pokémon", "PKMN" or "Pocket Monsters", so a whole collection
   does not crowd the list; put anything else in `Webmon/roms`). Saves work the
   same way (`Webmon/saves`, RetroArch `saves/`, `.sav` or `.srm`), as do mod
   zips and carts.
4. Mods that import from an N64 ROM (such as the
   [Stadium 2 importer](https://github.com/Deftones565/gen1recomp-mod-stadium2-importer))
   get the same picker for it: it lists the N64 ROMs you uploaded (the page's
   **N64 ROMs** tab), the ones in the USB `Webmon/n64` folder and the Pokemon
   ones in RetroArch `roms/` folders (`.z64`, `.n64` or `.v64`).

Press **1** on the remote (or a controller's Home button) at any time for
Webmon's menu: the upload page address and PIN, the game's keyboard shortcuts
and quitting (see [Controls](#controls)). The address also shows for a few
seconds at start.

## The upload page

<img src="docs/images/web-page.png" alt="The upload page" width="420" align="right">

Port 47811 (or the next free one). It only answers devices on your home
network that send the PIN. It shows:

- **TV**: free space, root access (the app running as root, or root commands
  through the Homebrew Channel), whether the TV can download by itself (curl),
  and which USB drive Webmon uses;
- **USB drives**: free space and access, and a button to create a `Webmon`
  folder on a drive;
- **The game**: download the latest release, or any listed release, onto the
  TV, or download it to your computer;
- **Memory for heavy mods**: the TV's memory, and an optional swap file (below);
- **Mod data**: each game's per-playthrough mod folders, with their size, to
  delete old map caches or send one built on a computer (below);
- **Game saves**: download the game's save slots and the `.sav` battery saves
  it exported;
- **Files**: upload, list, download and delete the game, ROMs, saves, mod zips,
  carts and N64 ROMs, to TV storage or the USB `Webmon` folder (RetroArch files
  can be downloaded, not deleted). While the game's picker is open the page
  says what it is waiting for, and new uploads appear in the picker by
  themselves.

The game's own downloads (mod catalogue, updates) also use the TV's curl.

## Where files go

| Place | Contents |
|---|---|
| `<USB>/Webmon/` | the `.love`; `roms/`, `saves/`, `mod-zips/`, `carts/`, `n64/` for the game's import screens |
| `<USB>/Webmon/game-data/` | the game's own data: saves, settings, installed mods and its import cache (the decoded graphics and sound of each imported game, the biggest part). Used when the drive can be written; the first time, the data already on the TV is moved there |
| `<USB>/roms/`, `<USB>/saves/` | RetroArch-style folders (any capitalisation, up to three levels deep): Pokemon titles (Game Boy and N64 ROMs, saves) are listed by the picker, nothing is changed |
| Data folder `game/`, `inbox/`, `save/` | the same, on TV storage, when there is no writable USB `Webmon` folder (or you choose TV storage) |

Keep the drive plugged in: without it Webmon falls back to the game data on TV
storage, which is separate.

When the app cannot read a USB drive itself, Webmon uses root (if available) to
bind-mount the `Webmon` folder into the data folder and to copy picked files.

The data folder is
`/media/developer/apps/usr/palm/applications/org.webmon.app/data`, with
`stdout.log` and `stderr.log`. It survives app updates but not uninstalling.

## Memory and heavy mods

webOS restarts the app when the TV runs short of free memory. Big mods can get
there: a voxel world mod (such as PotatoVoxel) building its map cache creates a
lot of 3D data at once. Webmon helps in two ways:

- **Prompt clean-up.** LÖVE objects (meshes, image data) hold memory that Lua's
  garbage collector does not see, so it would wait too long to free them.
  Webmon checks the process size every second and runs a full clean-up after
  it grows by 48 MB, or when the TV has less than 160 MB available. Done in
  one go a clean-up would stop the game (650-950 ms in a voxel town, where the
  game makes about 100 MB of garbage a second), so it is spread over frames:
  a quarter of each frame's time until it is done (a second of work takes
  about 4 seconds at 10 fps). Clean-ups also get a time budget, at most 5% of
  play time (after 900 ms of work the next waits 18 seconds), with Lua's own
  collector working in between, and back off while they free little. Near
  the critical level below none of this applies: a full clean-up runs at once,
  every 5 seconds.
  The log (`stdout.log`) records each clean-up with its pause and every new
  peak, plus the frame rate every 15 seconds and, once, the GPU driver, the
  window size and whether LuaJIT's compiler is on.
- **A fresh start before the TV freezes.** When the TV still has less than
  130 MB available after three clean-ups in a row, Webmon shows a 15-second
  countdown, then quits the game cleanly and starts again as a new process,
  which hands all its memory back to the TV. OK restarts at once; Back keeps
  playing (it asks again after two minutes if memory stays that low). Unsaved
  progress is lost, but a voxel cache build continues where it stopped, and
  the logs carry on across the restart.
- **A swap file (root, optional).** webOS TVs usually have compressed swap of
  their own (the upload page shows it). The page can add a swap file of 512 MB
  to 2 GB in the USB `Webmon` folder; Webmon switches it on whenever it runs
  and off when it closes. Flash drives are slow and wear with writing, so use
  it only if a mod needs it; FAT32 and ext4 drives work, NTFS cannot hold swap.

If the app is restarted in the middle of a cache build, choose the mod's
prebuild again: PotatoVoxel resumes where it stopped. A lower quality preset
(LOW or POTATO) also needs less memory. A first build can take long enough for
the TV's own power saving to switch it off (LG: Settings > General > System >
Eco / Power Saving, "Auto Power Off"); turn that off for the build or press a
remote button now and then.

Mods keep their data per game and playthrough (`mod_storage/<game>/<id>/` in
the game's data). When the game gives a playthrough a new id, for example after
the app was closed during a build, a voxel mod starts a new cache and the old
one stays behind. The upload page's **Mod data** section lists these folders
with their size and date, and deletes old ones. Save in the game once and
continue that save before a cache build: an unsaved new game gets a new id
every time the game starts.

A mod's cache is not tied to the TV, so it can be built on a computer and
uploaded:

1. Run the same gen1recomp release with LÖVE 11.5 on the computer, import the
   same ROM, install the same mod version and build the cache there.
2. On the upload page, use **Upload folder** on the playthrough you play on
   the TV and choose the computer's playthrough folder, in `mod_storage/<game>/`
   of LÖVE's save folder for `pokemon-love2d` (macOS:
   `~/Library/Application Support/LOVE/pokemon-love2d`, Windows:
   `%APPDATA%\LOVE\pokemon-love2d`, Linux: `~/.local/share/love/pokemon-love2d`).
   A mod folder inside it works too. The page sends the files one by one; they
   collect in a staging folder and only replace anything once all arrived.
   Each mod folder sent replaces that mod's data on the TV; other mods are kept.
3. Restart Webmon.

If anything differs (game release, mod version, ROM), the mod treats the
cache as stale and builds it again.

## Speed

LÖVE 11.5 switches LuaJIT's compiler off on every ARM device (on some phones
its machine code could not be placed where it needs to be), so the game would
run interpreted, several times slower; a voxel mod's 3D view shows it most.
Webmon switches the compiler back on before the game starts. If it ever
misbehaves, the menu's **Lua compiler** entry turns it off (remembered across
starts) and on again. `stdout.log` shows, every 15 seconds, the frame rate,
the slowest frame, the clean-ups' pauses, how many traces the compiler
built or abandoned, and Webmon's memory with the Lua heap's share of it.

For voxel mods on the TV's GPU, PotatoVoxel's LOW or POTATO preset (lower
render scale) and switching its shadows off help most.

## Controls

A game controller works as in gen1recomp. The TV remote:

| Remote | Game | | Remote | Game |
|---|---|---|---|---|
| Arrows | D-pad | | Yellow | Select |
| OK, Green | A | | Blue, 0, Play | Start |
| Back, Red | B | | CH+ / CH- | L / R |
| 1 | Webmon's menu | | Exit | Close Webmon |

A controller's **Home** (guide) button, which the game leaves unused, opens
the menu too. The menu shows the upload page address and PIN, and gives a
remote or controller the game's keyboard-only shortcuts:

| Menu entry | Keyboard key it sends |
|---|---|
| Quick save / Quick load | F1 / F2 |
| Tilt (3D view) | 3 |
| Voxel view, next step (PotatoVoxel) | 8 |
| Zoom level; zoom out / in | 4; - / = |
| Camera farther / closer (voxel mods) | E / Q |
| Colours | 2 |
| Game speed slower / faster | 0 / 1 |
| Mod manager | F10 |
| Voxel debug overlay (PotatoVoxel's timings) | F9 |
| Lua compiler on / off (see [Speed](#speed)) | (Webmon setting) |
| Quit Webmon | (asks for a second OK) |

Up/Down choose, OK runs it, Left/Right adjust the "/" entries, Back (B), 1 or
Home close it. A shortcut does nothing where the game does not allow it (tilt
only in free roam, for instance).

In Webmon's picker: Up/Down move, Left/Right page, OK chooses, Back cancels.

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
- `lua5.1 tools/bridge-test/test.lua src/lua/bridge.lua` checks the picker, the menu and the memory countdown.

## How it works

| Part | What it does |
|---|---|
| `src/main.cpp` | LÖVE's own `main` (from `src/love.cpp`) plus the webOS start-up; runs the newest `.love` or the setup screen, and again after the game asks LÖVE to restart |
| `src/webos/Platform.cpp` | Data folder, logs, the remote mapped onto the game's keys, the info panel key |
| `src/webos/Usb.cpp` | Root detection (Homebrew Channel exec), USB drives, the `Webmon` folder |
| `src/webos/Library.cpp` | Where files live, linking the USB `Webmon` folder, the game's data folder (`XDG_DATA_HOME`), file lists including RetroArch folders, mod data and game saves |
| `src/webos/Downloader.cpp` | Downloads gen1recomp releases with the TV's curl |
| `src/webos/Memory.cpp`, `src/webos/Swap.cpp` | Memory figures; the optional USB swap file (root) |
| `src/server/WebServer.cpp`, `web/index.html` | The upload page (cpp-httplib) |
| `src/webos/LuaBridge.cpp`, `src/lua/bridge.lua` | Lua API for the game and setup screen; the file picker gen1recomp asks for (`love.system.pickFile` / `getPickedFile`, including mods' required imports), the menu and memory management |
| `setup/` | The setup screen (Lua) |

## Licence

MIT (see [LICENSE](LICENSE)) for Webmon's own code. See
[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) for the bundled libraries.
Pokemon and all related names are the property of Nintendo, Creatures Inc. and
GAME FREAK inc. Webmon is not affiliated with them or with BOIS CLUB GAMES.
