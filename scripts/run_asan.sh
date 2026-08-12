#!/bin/bash
set -euo pipefail

if [[ "$(uname -s)" != "Darwin" ]]; then
    echo "run_asan.sh requires macOS" >&2
    exit 1
fi

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(dirname "${SCRIPT_DIR}")"
CLT_ROOT="${SLARK_MACOS_CLT_ROOT:-/Library/Developer/CommandLineTools}"
BUILD_DIR="${SLARK_ASAN_BUILD_DIR:-${ROOT_DIR}/cmake-build-ASan-test}"
BUILD_JOBS="${BUILD_JOBS:-$(sysctl -n hw.logicalcpu 2>/dev/null || echo 4)}"

if [[ -n "${SLARK_ASAN_CC:-}" || -n "${SLARK_ASAN_CXX:-}" || -n "${SLARK_MACOS_SDKROOT:-}" ]]; then
    echo "Custom ASan compiler/SDK overrides are not supported; use SLARK_MACOS_CLT_ROOT instead." >&2
    exit 1
fi

if [[ ! -x "${CLT_ROOT}/usr/bin/clang" || ! -x "${CLT_ROOT}/usr/bin/clang++" ]]; then
    echo "CommandLineTools clang not found under ${CLT_ROOT}" >&2
    exit 1
fi

CC="${CLT_ROOT}/usr/bin/clang"
CXX="${CLT_ROOT}/usr/bin/clang++"
SDKROOT="${CLT_ROOT}/SDKs/MacOSX.sdk"

if [[ ! -d "${SDKROOT}" ]]; then
    echo "macOS SDK not found: ${SDKROOT}" >&2
    exit 1
fi

compiler_matches_cache() {
    local variable="$1"
    local compiler="$2"
    grep -Fq "${variable}:FILEPATH=${compiler}" "${BUILD_DIR}/CMakeCache.txt" ||
        grep -Fq "${variable}:STRING=${compiler}" "${BUILD_DIR}/CMakeCache.txt"
}

if [[ -f "${BUILD_DIR}/CMakeCache.txt" ]] && {
    ! compiler_matches_cache CMAKE_C_COMPILER "${CC}" ||
    ! compiler_matches_cache CMAKE_CXX_COMPILER "${CXX}";
}; then
    echo "${BUILD_DIR} uses a different compiler; choose a new SLARK_ASAN_BUILD_DIR." >&2
    exit 1
fi

unset DYLD_INSERT_LIBRARIES
unset DYLD_LIBRARY_PATH
unset MallocStackLogging
unset MallocStackLoggingNoCompact
export ASAN_OPTIONS="detect_leaks=0:abort_on_error=1:halt_on_error=1:symbolize=1"

SANITIZER_FLAGS="-fsanitize=address -fno-omit-frame-pointer"

cmake -S "${ROOT_DIR}" -B "${BUILD_DIR}" -G "Unix Makefiles" \
    -DCMAKE_BUILD_TYPE=Debug \
    -DCMAKE_C_COMPILER="${CC}" \
    -DCMAKE_CXX_COMPILER="${CXX}" \
    -DCMAKE_OSX_SYSROOT="${SDKROOT}" \
    -DCMAKE_C_FLAGS="${SANITIZER_FLAGS}" \
    -DCMAKE_CXX_FLAGS="${SANITIZER_FLAGS}" \
    -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=address" \
    -DCMAKE_SHARED_LINKER_FLAGS="-fsanitize=address" \
    -DCMAKE_MODULE_LINKER_FLAGS="-fsanitize=address" \
    -DSLARK_FETCHCONTENT_BASE_DIR="${BUILD_DIR}/fetchcontent" \
    -DFETCHCONTENT_SOURCE_DIR_GOOGLETEST="${ROOT_DIR}/.fetchcontent_cache/googletest-src" \
    -DBUILD_PLATFORM=PC \
    -DDISABLE_HTTP=OFF

cmake --build "${BUILD_DIR}" --target Base_test http_test -j"${BUILD_JOBS}"

run_test() {
    local test_binary="$1"
    shift
    local binary="${BUILD_DIR}/${test_binary}"
    if [[ ! -x "${binary}" ]]; then
        echo "ASan test binary not found: ${binary}" >&2
        exit 1
    fi
    echo "Running ${binary}"
    "${binary}" --gtest_color=no "$@"
}

run_test test/base/Base_test "$@"
run_test test/http/http_test "$@"
