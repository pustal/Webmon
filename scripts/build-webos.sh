#!/usr/bin/env bash
# build-webos.sh
#
# One-shot build of the Webmon IPK: builds LÖVE and its libraries first if
# needed (scripts/build-deps.sh), then the launcher, then the package.
#
# Requirements: as scripts/build-deps.sh, plus ares-package from
# @webos-tools/cli (npm install -g @webos-tools/cli).
#   export WEBOS_SDK=$HOME/arm-webos-linux-gnueabi_sdk-buildroot
#
# Usage: scripts/build-webos.sh [extra cmake args...]
#        WEBMON_DEPS=<dir> to keep the dependency build elsewhere.
# Output: dist/org.webmon.app_<version>_arm.ipk and its Homebrew Channel manifest.

set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
: "${WEBOS_SDK:?Set WEBOS_SDK to the unpacked arm-webos-linux-gnueabi_sdk-buildroot directory}"
deps="${WEBMON_DEPS:-${repo_root}/third_party/deps-work}"

"${repo_root}/scripts/build-deps.sh" "${deps}"

cmake -S "${repo_root}" -B "${repo_root}/build-webos" \
    -DCMAKE_TOOLCHAIN_FILE="${WEBOS_SDK}/share/buildroot/toolchainfile.cmake" \
    -DCMAKE_BUILD_TYPE=Release \
    -DWEBMON_DEPS_DIR="${deps}" \
    "$@"
cmake --build "${repo_root}/build-webos" --parallel
cmake --build "${repo_root}/build-webos" --target webos-package
