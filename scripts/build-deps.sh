#!/usr/bin/env bash
# build-deps.sh
#
# Cross-compiles LÖVE 11.5 and everything it needs for LG webOS TVs (32-bit
# ARM, softfp, glibc 2.12) with the webosbrew toolchain, into one prefix:
#
#   LuaJIT (the commit gen1recomp pins), zlib (from the sysroot), libogg,
#   libvorbis, libtheora, libmodplug, FreeType, OpenAL Soft (SDL2 output, so
#   audio goes through SDL-webOS) and LÖVE itself (liblove.so + love).
#
# The libraries are built static and linked into liblove.so, so the app only
# ships liblove.so, libluajit, libSDL2 and the love executable.
#
# Requirements: WEBOS_SDK, git, curl, CMake, make, autoconf/automake/libtool,
# and a 32-bit host compiler for LuaJIT's build tools (gcc-multilib).
#
# Usage: scripts/build-deps.sh [work directory, default: third_party/deps-work]
# Output: <work>/prefix/{bin,lib,include}

set -euo pipefail

# ---- Pinned sources ----
readonly LUAJIT_REPO="https://github.com/LuaJIT/LuaJIT.git"
readonly LUAJIT_COMMIT="c6ffc141a8762b41703f9287d63d93622a13dd8f"   # gen1recomp's pin (scripts/luajit/pins.sh)
readonly LOVE_URL="https://github.com/love2d/love/releases/download/11.5/love-11.5-linux-src.tar.gz"
readonly OGG_URL="https://github.com/xiph/ogg/releases/download/v1.3.5/libogg-1.3.5.tar.gz"
readonly VORBIS_URL="https://github.com/xiph/vorbis/releases/download/v1.3.7/libvorbis-1.3.7.tar.gz"
readonly THEORA_REPO="https://github.com/xiph/theora.git"
readonly THEORA_TAG="v1.2.0"   # the first tag whose configure.ac bootstraps with current autotools
readonly MODPLUG_URL="https://sourceforge.net/projects/modplug-xmms/files/libmodplug/0.8.9.0/libmodplug-0.8.9.0.tar.gz/download"
readonly FREETYPE_REPO="https://github.com/freetype/freetype.git"
readonly FREETYPE_TAG="VER-2-13-3"
readonly OPENAL_REPO="https://github.com/kcat/openal-soft.git"
readonly OPENAL_TAG="1.23.1"
# SDL-webOS, as bundled by the other webOS apps (headers and library to link against).
readonly SDL_URL="https://github.com/webosbrew/SDL-webOS/releases/download/release-2.30.12-webos.6/SDL2-2.30.12-webos.tar.gz"

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
work="${1:-${repo_root}/third_party/deps-work}"
: "${WEBOS_SDK:?Set WEBOS_SDK to the unpacked arm-webos-linux-gnueabi_sdk-buildroot directory}"

readonly triple="arm-webos-linux-gnueabi"
readonly sysroot="${WEBOS_SDK}/${triple}/sysroot"
src="${work}/src"
prefix="${work}/prefix"
mkdir -p "${src}" "${prefix}/lib" "${prefix}/include" "${prefix}/bin"
jobs="$(nproc)"

host_pkg_config="$(command -v pkg-config)"
export PATH="${WEBOS_SDK}/bin:${PATH}"
export CC="${triple}-gcc" CXX="${triple}-g++" AR="${triple}-ar" RANLIB="${triple}-ranlib" STRIP="${triple}-strip"
export CFLAGS="-O2 -fPIC -I${prefix}/include"
# glibc 2.12's <inttypes.h>/<stdint.h> only define PRId64 & co. for C++ when asked to.
export CXXFLAGS="-O2 -fPIC -I${prefix}/include -D__STDC_FORMAT_MACROS -D__STDC_CONSTANT_MACROS -D__STDC_LIMIT_MACROS"
export CPPFLAGS="-I${prefix}/include"
export LDFLAGS="-L${prefix}/lib"
# Only our prefix (plus the sysroot's zlib) is visible to pkg-config.
export PKG_CONFIG_LIBDIR="${prefix}/lib/pkgconfig"
export PKG_CONFIG_SYSROOT_DIR=""
# The host's pkg-config: the SDK's wrapper would prefix every path with the sysroot.
export PKG_CONFIG="${host_pkg_config}"
autotools_host=(--host=arm-linux-gnueabi --prefix="${prefix}" --disable-shared --enable-static)

say() { printf '\033[1;32m==>\033[0m %s\n' "$*"; }

# fetch_tar <url> <dir name>: downloads and unpacks a release tarball once.
fetch_tar() {
    local url="$1" name="$2"
    [[ -d "${src}/${name}" ]] && return
    mkdir -p "${src}/${name}"
    curl -sSfL "${url}" | tar -xz -C "${src}/${name}" --strip-components=1
}

# fetch_git <repo> <ref> <dir name>: shallow clone of one tag or commit.
fetch_git() {
    local repo="$1" ref="$2" name="$3"
    [[ -d "${src}/${name}/.git" ]] && return
    git init -q "${src}/${name}"
    git -C "${src}/${name}" fetch -q --depth 1 "${repo}" "${ref}"
    git -C "${src}/${name}" checkout -q FETCH_HEAD
}

# built <name>: true if <name> was already built into the prefix.
built() { [[ -f "${work}/.built-$1" ]]; }
mark() { touch "${work}/.built-$1"; }

# ---- SDL-webOS ----
if ! built sdl; then
    say "SDL-webOS"
    fetch_tar "${SDL_URL}" sdl
    cp -a "${src}/sdl/include/." "${prefix}/include/"
    cp -a "${src}/sdl/lib/libSDL2"*.so* "${prefix}/lib/"
    mkdir -p "${prefix}/lib/pkgconfig"
    cat > "${prefix}/lib/pkgconfig/sdl2.pc" <<EOF
prefix=${prefix}
Name: sdl2
Description: SDL-webOS
Version: 2.30.12
Libs: -L\${prefix}/lib -lSDL2
Cflags: -I\${prefix}/include/SDL2 -D_REENTRANT -D__WEBOS__
EOF
    mark sdl
fi

# ---- zlib: the sysroot's, exposed through pkg-config ----
if ! built zlib; then
    cat > "${prefix}/lib/pkgconfig/zlib.pc" <<EOF
Name: zlib
Description: zlib from the webOS sysroot
Version: 1.2.13
Libs: -lz
Cflags:
EOF
    mark zlib
fi

# ---- LuaJIT (its build tools run on the host, as 32-bit like the target) ----
if ! built luajit; then
    say "LuaJIT ${LUAJIT_COMMIT:0:12}"
    fetch_git "${LUAJIT_REPO}" "${LUAJIT_COMMIT}" luajit
    # LuaJIT reads its version from the git checkout; record it for the shallow clone.
    git -C "${src}/luajit" show -s --format=%ct > "${src}/luajit/.relver"
    make -C "${src}/luajit" clean >/dev/null
    make -C "${src}/luajit" -j"${jobs}" amalg \
        HOST_CC="gcc -m32" CROSS="${triple}-" TARGET_SYS=Linux \
        TARGET_CFLAGS="-mfloat-abi=softfp -fPIC" BUILDMODE=dynamic
    make -C "${src}/luajit" install PREFIX="${prefix}" HOST_CC="gcc -m32" CROSS="${triple}-" \
        TARGET_SYS=Linux BUILDMODE=dynamic >/dev/null
    rm -f "${prefix}/lib/libluajit-5.1.a"
    mark luajit
fi

# ---- Ogg / Vorbis / Theora ----
if ! built ogg; then
    say "libogg"
    fetch_tar "${OGG_URL}" ogg
    (cd "${src}/ogg" && ./configure "${autotools_host[@]}" >/dev/null && make -j"${jobs}" >/dev/null && make install >/dev/null)
    mark ogg
fi
if ! built vorbis; then
    say "libvorbis"
    fetch_tar "${VORBIS_URL}" vorbis
    (cd "${src}/vorbis" && ./configure "${autotools_host[@]}" --with-ogg="${prefix}" >/dev/null &&
        make -j"${jobs}" >/dev/null && make install >/dev/null)
    mark vorbis
fi
if ! built theora; then
    say "libtheora ${THEORA_TAG}"
    fetch_git "${THEORA_REPO}" "${THEORA_TAG}" theora
    # The git tag has no generated configure script.
    (cd "${src}/theora" && autoreconf -fi >/dev/null 2>&1 &&
     ./configure "${autotools_host[@]}" --with-ogg="${prefix}" --disable-examples --disable-encode \
        --disable-oggtest --disable-vorbistest --disable-sdltest --disable-spec >/dev/null &&
        make -j"${jobs}" >/dev/null && make install >/dev/null)
    mark theora
fi

# ---- libmodplug ----
if ! built modplug; then
    say "libmodplug"
    fetch_tar "${MODPLUG_URL}" modplug
    (cd "${src}/modplug" && ./configure "${autotools_host[@]}" >/dev/null && make -j"${jobs}" >/dev/null &&
        make install >/dev/null)
    mark modplug
fi

# ---- FreeType ----
if ! built freetype; then
    say "FreeType ${FREETYPE_TAG}"
    fetch_git "${FREETYPE_REPO}" "${FREETYPE_TAG}" freetype
    cmake -S "${src}/freetype" -B "${work}/build-freetype" \
        -DCMAKE_TOOLCHAIN_FILE="${WEBOS_SDK}/share/buildroot/toolchainfile.cmake" \
        -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="${prefix}" \
        -DCMAKE_C_FLAGS="${CFLAGS}" -DCMAKE_CXX_FLAGS="${CXXFLAGS}" -DBUILD_SHARED_LIBS=OFF \
        -DCMAKE_POSITION_INDEPENDENT_CODE=ON \
        -DFT_DISABLE_HARFBUZZ=ON -DFT_DISABLE_BZIP2=ON -DFT_DISABLE_PNG=ON -DFT_DISABLE_BROTLI=ON \
        -DFT_REQUIRE_ZLIB=ON >/dev/null
    cmake --build "${work}/build-freetype" -j"${jobs}" >/dev/null
    cmake --install "${work}/build-freetype" >/dev/null
    mark freetype
fi

# ---- OpenAL Soft, with only the SDL2 output (SDL-webOS does the TV's audio) ----
if ! built openal; then
    say "OpenAL Soft ${OPENAL_TAG}"
    fetch_git "${OPENAL_REPO}" "${OPENAL_TAG}" openal
    cmake -S "${src}/openal" -B "${work}/build-openal" \
        -DCMAKE_TOOLCHAIN_FILE="${WEBOS_SDK}/share/buildroot/toolchainfile.cmake" \
        -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="${prefix}" \
        -DCMAKE_C_FLAGS="${CFLAGS}" -DCMAKE_CXX_FLAGS="${CXXFLAGS}" -DLIBTYPE=STATIC \
        -DCMAKE_POSITION_INDEPENDENT_CODE=ON \
        -DALSOFT_UTILS=OFF -DALSOFT_EXAMPLES=OFF -DALSOFT_TESTS=OFF -DALSOFT_INSTALL_CONFIG=OFF \
        -DALSOFT_INSTALL_HRTF_DATA=OFF -DALSOFT_INSTALL_AMBDEC_PRESETS=OFF \
        -DALSOFT_REQUIRE_SDL2=ON -DALSOFT_BACKEND_SDL2=ON \
        -DALSOFT_BACKEND_ALSA=OFF -DALSOFT_BACKEND_OSS=OFF -DALSOFT_BACKEND_PULSEAUDIO=OFF \
        -DALSOFT_BACKEND_PIPEWIRE=OFF -DALSOFT_BACKEND_JACK=OFF -DALSOFT_BACKEND_PORTAUDIO=OFF \
        -DALSOFT_BACKEND_SNDIO=OFF -DALSOFT_BACKEND_SOLARIS=OFF -DALSOFT_BACKEND_WAVE=OFF \
        -DSDL2_DIR="" -DSDL2_INCLUDE_DIR="${prefix}/include/SDL2" -DSDL2_LIBRARY="${prefix}/lib/libSDL2.so" \
        >/dev/null
    cmake --build "${work}/build-openal" -j"${jobs}" >/dev/null
    cmake --install "${work}/build-openal" >/dev/null
    mark openal
fi

# ---- LÖVE 11.5 ----
if ! built love; then
    say "LÖVE 11.5"
    fetch_tar "${LOVE_URL}" love
    # Static OpenAL Soft needs its C++ runtime and SDL; LÖVE carries a static libstdc++.
    (cd "${src}/love" &&
        ./configure --host=arm-linux-gnueabi --prefix="${prefix}" --with-lua=luajit --disable-mpg123 \
            --disable-exe \
            --disable-sdltest \
            LDFLAGS="-L${prefix}/lib -static-libstdc++ -static-libgcc -Wl,--exclude-libs,ALL" \
            openal_CFLAGS="-I${prefix}/include -DAL_LIBTYPE_STATIC" \
            openal_LIBS="-L${prefix}/lib -lopenal -lSDL2 -lpthread -ldl -lm" \
            vorbisfile_LIBS="-L${prefix}/lib -lvorbisfile -lvorbis -logg -lm" \
            >/dev/null &&
        make -j"${jobs}" >/dev/null && make install >/dev/null)
    mark love
fi

say "Done: ${prefix}"
