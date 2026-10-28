#!/usr/bin/env bash
# Builds the upload-page harness with the host compiler and runs it on <data folder> [version].
set -euo pipefail
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
data="${1:?usage: run.sh <data folder> [version]}"
out="${root}/build-harness"
mkdir -p "${out}" "${data}"
cmake -DINPUT="${root}/web/index.html" -DOUTPUT="${out}/WebPage.generated.h" -DVARIABLE=kWebPageHtml \
      -P "${root}/cmake/EmbedTextFile.cmake"
# miniz (zip reading) is C: compiled on its own, without our warnings.
gcc -O1 -w -c "${root}/third_party/miniz/miniz.c" -o "${out}/miniz.o"
g++ -std=c++17 -O1 -Wall -Wextra -I"${root}/src" -I"${out}" -isystem "${root}/third_party/cpp-httplib" \
    -isystem "${root}/third_party/miniz" \
    "${root}/tools/server-harness/harness.cpp" "${root}/src/server/WebServer.cpp" "${root}/src/webos/Downloader.cpp" \
    "${root}/src/webos/Json.cpp" "${root}/src/webos/Library.cpp" "${root}/src/webos/NetInfo.cpp" \
    "${root}/src/webos/Process.cpp" "${root}/src/webos/Usb.cpp" "${root}/src/webos/Swap.cpp" "${root}/src/webos/Memory.cpp" \
    "${root}/src/webos/ZipReader.cpp" "${out}/miniz.o" -lpthread -o "${out}/harness"
exec "${out}/harness" "${data}" "${2:-harness}"
