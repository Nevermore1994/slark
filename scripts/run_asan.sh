#!/bin/bash
# Usage: [SLARK_SANITIZER=address|thread] scripts/run_asan.sh [gtest args]
set -euo pipefail

if [[ "$(uname -s)" != "Darwin" ]]; then
    echo "run_asan.sh requires macOS" >&2
    exit 1
fi

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(dirname "${SCRIPT_DIR}")"
CLT_ROOT="${SLARK_MACOS_CLT_ROOT:-/Library/Developer/CommandLineTools}"
SANITIZER="${SLARK_SANITIZER:-address}"
BUILD_JOBS="${BUILD_JOBS:-$(sysctl -n hw.logicalcpu 2>/dev/null || echo 4)}"

case "${SANITIZER}" in
    address)
        BUILD_NAME="ASan"
        TARGETS=(Base_test http_test Engine_test Core_test)
        ;;
    thread)
        BUILD_NAME="TSan"
        TARGETS=(Base_test Engine_test Core_test)
        ;;
    *)
        echo "SLARK_SANITIZER must be address or thread, got: ${SANITIZER}" >&2
        exit 1
        ;;
esac
BUILD_DIR="${SLARK_SANITIZER_BUILD_DIR:-${ROOT_DIR}/cmake/build/${BUILD_NAME}-test}"

if [[ -n "${SLARK_ASAN_CC:-}" || -n "${SLARK_ASAN_CXX:-}" || -n "${SLARK_MACOS_SDKROOT:-}" ]]; then
    echo "Custom sanitizer compiler/SDK overrides are not supported; use SLARK_MACOS_CLT_ROOT instead." >&2
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
    local type
    # CMake keeps a compiler passed with a bare -D as UNINITIALIZED.
    for type in FILEPATH STRING UNINITIALIZED; do
        if grep -Fqx "${variable}:${type}=${compiler}" "${BUILD_DIR}/CMakeCache.txt"; then
            return 0
        fi
    done
    return 1
}

if [[ -f "${BUILD_DIR}/CMakeCache.txt" ]] && {
    ! compiler_matches_cache CMAKE_C_COMPILER "${CC}" ||
    ! compiler_matches_cache CMAKE_CXX_COMPILER "${CXX}";
}; then
    echo "${BUILD_DIR} uses a different compiler; choose a new SLARK_SANITIZER_BUILD_DIR." >&2
    exit 1
fi

unset DYLD_INSERT_LIBRARIES
unset DYLD_LIBRARY_PATH
unset MallocStackLogging
unset MallocStackLoggingNoCompact
export ASAN_OPTIONS="detect_leaks=0:abort_on_error=1:halt_on_error=1:symbolize=1"
export TSAN_OPTIONS="halt_on_error=0:second_deadlock_stack=1:symbolize=1"

SANITIZER_FLAGS="-fsanitize=${SANITIZER} -fno-omit-frame-pointer"

cmake -S "${ROOT_DIR}" -B "${BUILD_DIR}" -G "Unix Makefiles" \
    -DCMAKE_BUILD_TYPE=Debug \
    -DCMAKE_C_COMPILER="${CC}" \
    -DCMAKE_CXX_COMPILER="${CXX}" \
    -DCMAKE_OSX_SYSROOT="${SDKROOT}" \
    -DCMAKE_C_FLAGS="${SANITIZER_FLAGS}" \
    -DCMAKE_CXX_FLAGS="${SANITIZER_FLAGS}" \
    -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=${SANITIZER}" \
    -DCMAKE_SHARED_LINKER_FLAGS="-fsanitize=${SANITIZER}" \
    -DCMAKE_MODULE_LINKER_FLAGS="-fsanitize=${SANITIZER}" \
    -DSLARK_FETCHCONTENT_BASE_DIR="${BUILD_DIR}/fetchcontent" \
    -DFETCHCONTENT_SOURCE_DIR_GOOGLETEST="${ROOT_DIR}/.fetchcontent_cache/googletest-src" \
    -DBUILD_PLATFORM=PC \
    -DDISABLE_HTTP=OFF

cmake --build "${BUILD_DIR}" --target "${TARGETS[@]}" -j"${BUILD_JOBS}"

test_binary() {
    case "$1" in
        Base_test) echo "test/base/Base_test" ;;
        http_test) echo "test/http/http_test" ;;
        Engine_test) echo "test/engine/Engine_test" ;;
        Core_test) echo "test/core/Core_test" ;;
    esac
}

failed=()

run_test() {
    local target="$1"
    shift
    local relative
    relative="$(test_binary "${target}")"
    local binary="${BUILD_DIR}/${relative}"
    if [[ ! -x "${binary}" ]]; then
        echo "Sanitizer test binary not found: ${binary}" >&2
        failed+=("${target}")
        return
    fi
    echo "Running ${binary}"
    if ! (cd "${ROOT_DIR}/$(dirname "${relative}")" && "${binary}" --gtest_color=no "$@"); then
        failed+=("${target}")
    fi
}

for target in "${TARGETS[@]}"; do
    run_test "${target}" "$@"
done

if ((${#failed[@]} > 0)); then
    echo "Failed under ${SANITIZER} sanitizer: ${failed[*]}" >&2
    exit 1
fi
