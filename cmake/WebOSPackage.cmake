# WebOSPackage.cmake
#
# Adds the "webos-package" target. It stages the IPK layout under <build>/ipk/:
#   appinfo.json, icons
#   bin/webmon                      the launcher (stripped)
#   lib/liblove-11.5.so             LÖVE 11.5 (OpenAL Soft, FreeType, Ogg/Vorbis/Theora,
#                                   modplug linked in)
#   lib/libluajit-5.1.so.2          LuaJIT
#   lib/libSDL2-2.0.so.0            SDL-webOS
#   lib/libstdc++.so.6, libgcc_s.so.1   the toolchain's C++ runtime (the TV's is older)
#   share/setup/                    the setup screen (Lua)
#   LICENSE, THIRD_PARTY_NOTICES.md, LICENSE-cpp-httplib.txt
# then builds dist/<id>_<version>_arm.ipk with ares-package (@webos-tools/cli)
# and the Homebrew Channel manifest next to it.
#
# No game is packaged: the player supplies gen1recomp's .love and a ROM.

find_program(WEBMON_ARES_PACKAGE ares-package)

set(WEBMON_IPK_STAGE "${CMAKE_CURRENT_BINARY_DIR}/ipk")
set(WEBMON_DIST_DIR "${WEBMON_ROOT}/dist")
set(WEBMON_IPK_NAME "${WEBMON_APP_ID}_${PROJECT_VERSION}_arm.ipk")

# The toolchain's C++ runtime, found next to its compiler.
get_filename_component(_webmon_compiler_dir "${CMAKE_CXX_COMPILER}" DIRECTORY)
set(WEBMON_RUNTIME_DIR "${_webmon_compiler_dir}/../arm-webos-linux-gnueabi/lib")

configure_file("${WEBMON_ROOT}/webos/appinfo.json.in" "${CMAKE_CURRENT_BINARY_DIR}/appinfo.json" @ONLY)

if(NOT CMAKE_STRIP)
    set(CMAKE_STRIP strip)
endif()

set(_stage_lib "${WEBMON_IPK_STAGE}/lib")
add_custom_target(webos-stage
    COMMAND "${CMAKE_COMMAND}" -E rm -rf "${WEBMON_IPK_STAGE}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${WEBMON_IPK_STAGE}/bin" "${_stage_lib}" "${WEBMON_IPK_STAGE}/share"
    COMMAND "${CMAKE_COMMAND}" -E copy "${CMAKE_CURRENT_BINARY_DIR}/appinfo.json" "${WEBMON_IPK_STAGE}/"
    COMMAND "${CMAKE_COMMAND}" -E copy "${WEBMON_ROOT}/webos/icon.png" "${WEBMON_ROOT}/webos/largeIcon.png" "${WEBMON_IPK_STAGE}/"
    COMMAND "${CMAKE_COMMAND}" -E copy "$<TARGET_FILE:webmon>" "${WEBMON_IPK_STAGE}/bin/webmon"
    COMMAND "${CMAKE_STRIP}" --strip-unneeded "${WEBMON_IPK_STAGE}/bin/webmon"
    COMMAND "${CMAKE_COMMAND}" -E copy
            "${WEBMON_DEPS_PREFIX}/lib/liblove-11.5.so"
            "${WEBMON_DEPS_PREFIX}/lib/libluajit-5.1.so.2"
            "${WEBMON_DEPS_PREFIX}/lib/libSDL2-2.0.so.0"
            "${WEBMON_RUNTIME_DIR}/libstdc++.so.6"
            "${WEBMON_RUNTIME_DIR}/libgcc_s.so.1"
            "${_stage_lib}/"
    COMMAND "${CMAKE_STRIP}" --strip-unneeded
            "${_stage_lib}/liblove-11.5.so" "${_stage_lib}/libluajit-5.1.so.2" "${_stage_lib}/libSDL2-2.0.so.0"
            "${_stage_lib}/libstdc++.so.6" "${_stage_lib}/libgcc_s.so.1"
    COMMAND "${CMAKE_COMMAND}" -E copy_directory "${WEBMON_ROOT}/setup" "${WEBMON_IPK_STAGE}/share/setup"
    COMMAND "${CMAKE_COMMAND}" -E copy "${WEBMON_ROOT}/LICENSE" "${WEBMON_ROOT}/THIRD_PARTY_NOTICES.md" "${WEBMON_IPK_STAGE}/"
    COMMAND "${CMAKE_COMMAND}" -E copy "${WEBMON_ROOT}/third_party/cpp-httplib/LICENSE" "${WEBMON_IPK_STAGE}/LICENSE-cpp-httplib.txt"
    DEPENDS webmon
    COMMENT "Staging the IPK contents in ${WEBMON_IPK_STAGE}"
    VERBATIM)

if(WEBMON_ARES_PACKAGE)
    add_custom_target(webos-package
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${WEBMON_DIST_DIR}"
        COMMAND "${WEBMON_ARES_PACKAGE}" --no-minify "${WEBMON_IPK_STAGE}" -o "${WEBMON_DIST_DIR}"
        COMMAND "${CMAKE_COMMAND}"
                -DIPK=${WEBMON_DIST_DIR}/${WEBMON_IPK_NAME}
                -DOUTPUT=${WEBMON_DIST_DIR}/${WEBMON_APP_ID}.manifest.json
                -DAPP_ID=${WEBMON_APP_ID}
                -DVERSION=${PROJECT_VERSION}
                "-DTITLE=Webmon"
                "-DDESCRIPTION=Runs gen1recomp (Pokemon Gen 1 Recompilation Project) on webOS. Bring the game's .love release and your own ROM."
                -DICON_URI=https://raw.githubusercontent.com/pustal/Webmon/main/webos/largeIcon.png
                -DSOURCE_URL=https://github.com/pustal/Webmon
                -P "${WEBMON_ROOT}/cmake/WriteManifest.cmake"
        DEPENDS webos-stage
        COMMENT "Building ${WEBMON_IPK_NAME}"
        VERBATIM)
else()
    add_custom_target(webos-package
        COMMAND "${CMAKE_COMMAND}" -E echo "ares-package not found: npm install -g @webos-tools/cli"
        COMMAND "${CMAKE_COMMAND}" -E false
        DEPENDS webos-stage)
endif()
