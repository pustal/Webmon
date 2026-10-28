# Webmon: gen1recomp (a LÖVE game) on LG webOS TVs.
#
#   1. scripts/build-deps.sh                  LÖVE 11.5 and its libraries for webOS
#   2. cmake -B build-webos -DCMAKE_TOOLCHAIN_FILE=<sdk>/share/buildroot/toolchainfile.cmake
#            -DWEBMON_DEPS_DIR=<deps work dir>
#      cmake --build build-webos --target webos-package
# or scripts/build-webos.sh for both.

cmake_minimum_required(VERSION 3.21)
project(Webmon VERSION 0.3.6 LANGUAGES C CXX)

set(WEBMON_ROOT "${CMAKE_CURRENT_SOURCE_DIR}")
list(APPEND CMAKE_MODULE_PATH "${WEBMON_ROOT}/cmake")

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

set(WEBMON_APP_ID "org.webmon.app" CACHE STRING "webOS application id")

# ---- LÖVE and its libraries, from scripts/build-deps.sh ----
set(WEBMON_DEPS_DIR "${WEBMON_ROOT}/third_party/deps-work" CACHE PATH "Work directory of scripts/build-deps.sh")
set(WEBMON_DEPS_PREFIX "${WEBMON_DEPS_DIR}/prefix")
set(WEBMON_LOVE_SOURCE "${WEBMON_DEPS_DIR}/src/love/src")
if(NOT EXISTS "${WEBMON_DEPS_PREFIX}/lib/liblove-11.5.so" OR NOT EXISTS "${WEBMON_LOVE_SOURCE}/love.cpp")
    message(FATAL_ERROR "LÖVE not found in ${WEBMON_DEPS_DIR}: run scripts/build-deps.sh first")
endif()

add_library(Webmon::love SHARED IMPORTED)
set_target_properties(Webmon::love PROPERTIES
    IMPORTED_LOCATION "${WEBMON_DEPS_PREFIX}/lib/liblove-11.5.so"
    IMPORTED_SONAME "liblove-11.5.so"
    INTERFACE_INCLUDE_DIRECTORIES "${WEBMON_LOVE_SOURCE};${WEBMON_LOVE_SOURCE}/modules;${WEBMON_DEPS_PREFIX}/include/luajit-2.1;${WEBMON_DEPS_PREFIX}/include/SDL2")
add_library(Webmon::luajit SHARED IMPORTED)
set_target_properties(Webmon::luajit PROPERTIES
    IMPORTED_LOCATION "${WEBMON_DEPS_PREFIX}/lib/libluajit-5.1.so.2"
    IMPORTED_SONAME "libluajit-5.1.so.2")
add_library(Webmon::SDL2 SHARED IMPORTED)
set_target_properties(Webmon::SDL2 PROPERTIES
    IMPORTED_LOCATION "${WEBMON_DEPS_PREFIX}/lib/libSDL2-2.0.so.0"
    IMPORTED_SONAME "libSDL2-2.0.so.0"
    INTERFACE_COMPILE_DEFINITIONS "__WEBOS__")

# ---- Text files embedded in the launcher: the upload page and the Lua bridge ----
set(WEBMON_GENERATED_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
function(webmon_embed input output variable)
    add_custom_command(
        OUTPUT "${WEBMON_GENERATED_DIR}/${output}"
        COMMAND "${CMAKE_COMMAND}" -DINPUT=${WEBMON_ROOT}/${input} -DOUTPUT=${WEBMON_GENERATED_DIR}/${output}
                -DVARIABLE=${variable} -P "${WEBMON_ROOT}/cmake/EmbedTextFile.cmake"
        DEPENDS "${WEBMON_ROOT}/${input}" "${WEBMON_ROOT}/cmake/EmbedTextFile.cmake"
        COMMENT "Embedding ${input}")
endfunction()
webmon_embed(web/index.html WebPage.generated.h kWebPageHtml)
webmon_embed(src/lua/bridge.lua BridgeScript.generated.h kBridgeScript)

find_package(Threads REQUIRED)

# ---- The launcher ----
add_executable(webmon
    src/main.cpp
    src/server/WebServer.cpp
    src/webos/Downloader.cpp
    src/webos/Json.cpp
    src/webos/Library.cpp
    src/webos/LuaBridge.cpp
    src/webos/Memory.cpp
    src/webos/NetInfo.cpp
    src/webos/Platform.cpp
    src/webos/Process.cpp
    src/webos/Swap.cpp
    src/webos/Usb.cpp
    "${WEBMON_GENERATED_DIR}/WebPage.generated.h"
    "${WEBMON_GENERATED_DIR}/BridgeScript.generated.h"
)
target_compile_features(webmon PRIVATE cxx_std_17)
set_target_properties(webmon PROPERTIES CXX_EXTENSIONS OFF)
target_include_directories(webmon PRIVATE "${WEBMON_ROOT}/src" "${WEBMON_GENERATED_DIR}")
# cpp-httplib (MIT) is header-only; treat it as a system header so its warnings stay out of ours.
target_include_directories(webmon SYSTEM PRIVATE "${WEBMON_ROOT}/third_party/cpp-httplib")
target_compile_definitions(webmon PRIVATE WEBMON_VERSION="${PROJECT_VERSION}")
# -Wno-psabi: GCC notes an ARM ABI change from GCC 7 for std::vector arguments; it does not affect us.
target_compile_options(webmon PRIVATE -Wall -Wextra -Wshadow -Wconversion -Wno-sign-conversion -Wno-psabi)
target_link_libraries(webmon PRIVATE Webmon::love Webmon::luajit Webmon::SDL2 Threads::Threads)
set_target_properties(webmon PROPERTIES
    BUILD_WITH_INSTALL_RPATH ON
    INSTALL_RPATH "\$ORIGIN/../lib")

include(WebOSPackage)
