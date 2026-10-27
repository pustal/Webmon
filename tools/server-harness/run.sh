#!/usr/bin/env bash
# Builds the upload-page harness with the host compiler and runs it on <data folder>.
set -euo pipefail
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
data="${1:?usage: run.sh <data folder>}"
out="${root}/build-harness"
mkdir -p "${out}" "${data}"
cmake -DINPUT="${root}/web/index.html" -DOUTPUT="${out}/WebPage.generated.h" -DVARIABLE=kWebPageHtml \
      -P "${root}/cmake/EmbedTextFile.cmake"
g++ -std=c++17 -O1 -Wall -Wextra -I"${root}/src" -I"${out}" -isystem "${root}/third_party/cpp-httplib" \
    "${root}/tools/server-harness/harness.cpp" "${root}/src/server/WebServer.cpp" "${root}/src/webos/Downloader.cpp" \
    "${root}/src/webos/Json.cpp" "${root}/src/webos/Library.cpp" "${root}/src/webos/NetInfo.cpp" \
    "${root}/src/webos/Process.cpp" "${root}/src/webos/Usb.cpp" -lpthread -o "${out}/harness"
exec "${out}/harness" "${data}"
