# Third-party notices

Webmon contains no gen1recomp code and no game data. The player adds the
official gen1recomp release (`.love`) and their own ROM.

| Component | Where | Licence |
|---|---|---|
| [LÖVE](https://love2d.org) 11.5 | `lib/liblove-11.5.so`; `src/main.cpp` is based on LÖVE's `src/love.cpp` | zlib |
| [LuaJIT](https://luajit.org) (commit `c6ffc141`, as pinned by gen1recomp) | `lib/libluajit-5.1.so.2` | MIT |
| [SDL-webOS](https://github.com/webosbrew/SDL-webOS) 2.30.12 | `lib/libSDL2-2.0.so.0` | zlib |
| [OpenAL Soft](https://openal-soft.org) 1.23.1 | inside `liblove-11.5.so` | LGPL-2.1 (linked statically; the build script rebuilds it from source) |
| [FreeType](https://freetype.org) 2.13.3 | inside `liblove-11.5.so` | FreeType Licence |
| libogg 1.3.5, libvorbis 1.3.7, libtheora 1.2.0 ([Xiph.Org](https://xiph.org)) | inside `liblove-11.5.so` | BSD 3-Clause |
| [libmodplug](https://github.com/Konstanty/libmodplug) 0.8.9.0 | inside `liblove-11.5.so` | Public domain |
| GCC runtime (`libstdc++`, `libgcc_s`) from the webosbrew toolchain | `lib/` | GPL-3.0 with the GCC Runtime Library Exception |

Webmon runs [gen1recomp](https://github.com/bryanthaboi/gen1recomp):
"Based on the Pokemon Gen 1 Recompilation Project by BOIS CLUB GAMES, LLC
(https://github.com/bryanthaboi/gen1recomp)". Pokemon and all related names
are the property of Nintendo, Creatures Inc. and GAME FREAK inc. Webmon is not
affiliated with any of them.
