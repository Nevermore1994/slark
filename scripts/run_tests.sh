#!/bin/bash
# Run all tests in parallel (base / core / integration / http / platform / fuzz)
#
# Output: detailed results -> test/output/<YYYYMMDD_HHMMSS>/
#         terminal         -> per-group stats + final failed-test summary
#
# Usage:
#   ./scripts/run_tests.sh              # Debug build + run all tests
#   ./scripts/run_tests.sh Release      # Release build
#   ./scripts/run_tests.sh Debug -L base # run only base-label tests
#   ./scripts/run_tests.sh Debug --gtest_filter="LogConfig*" # filter specific tests
#
# Environment:
#   BUILD_JOBS=4 ./scripts/run_tests.sh # parallel compile jobs

set -o pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"

BUILD_TYPE="${1:-Debug}"
BUILD_DIR="${PROJECT_DIR}/cmake-build-${BUILD_TYPE}-test"
BUILD_JOBS="${BUILD_JOBS:-$(sysctl -n hw.logicalcpu 2>/dev/null || nproc 2>/dev/null || echo 4)}"

# Colors
GREEN='\033[0;32m'
RED='\033[0;31m'
YELLOW='\033[1;33m'
CYAN='\033[0;36m'
NC='\033[0m'

# Extra args passed to ctest (-L, --gtest_filter, etc.)
shift 2>/dev/null || true
CTEST_ARGS="$@"

# Output directory
TIMESTAMP="$(date +%Y%m%d_%H%M%S)"
OUTPUT_DIR="${PROJECT_DIR}/test/output/${TIMESTAMP}"
mkdir -p "${OUTPUT_DIR}"

echo -e "${CYAN}============================================${NC}"
echo -e "${CYAN} Slark Test Runner${NC}"
echo -e "${CYAN}============================================${NC}"
echo " Build Type : ${BUILD_TYPE}"
echo " Build Dir  : ${BUILD_DIR}"
echo " Build Jobs : ${BUILD_JOBS}"
echo " Output Dir : ${OUTPUT_DIR}"
echo " CTest Args : ${CTEST_ARGS:-<none>}"
echo -e "${CYAN}============================================${NC}"

# ─── 1. Configure ───────────────────────────────────────────────────────────────
echo ""
echo -e "${YELLOW}[1/3] Configuring CMake...${NC}"
cmake -S "${PROJECT_DIR}" \
      -B "${BUILD_DIR}" \
      -DCMAKE_BUILD_TYPE="${BUILD_TYPE}" \
      -DBUILD_PLATFORM=PC \
      -DDISABLE_HTTP=OFF

# ─── 2. Build ───────────────────────────────────────────────────────────────────
echo ""
echo -e "${YELLOW}[2/3] Building tests...${NC}"
TARGETS="Base_test Core_test http_test Platform_test CoreFuzz_test HttpFuzz_test StringUtilFuzz_test SeekContextFuzz_test TimerPoolFuzz_test FileStreamFuzz_test WriterFuzz_test ThreadFuzz_test"
if [ -d "${PROJECT_DIR}/src/core" ]; then
    TARGETS="${TARGETS} CoreIntegration_test PlayerMediaIntegration_test"
fi
# shellcheck disable=SC2086
cmake --build "${BUILD_DIR}" \
      --target ${TARGETS} \
      -j"${BUILD_JOBS}"

# ─── 3. Run ─────────────────────────────────────────────────────────────────────
echo ""
echo -e "${YELLOW}[3/3] Running tests in parallel...${NC}"
cd "${BUILD_DIR}"

# Parse label filter from ctest args; if -L given, run only matching labels
HAS_LABEL_FILTER=false
if echo "${CTEST_ARGS}" | grep -Eq '\-L[ =]'; then
    HAS_LABEL_FILTER=true
fi

run_test_group() {
    local labels="$1"
    local group_name="$2"
    local group_tag="$(echo "${labels}" | tr ' ' '_')"
    local output_file="${OUTPUT_DIR}/${group_tag}.log"
    local fail_file="${OUTPUT_DIR}/${group_tag}_failed.log"

    echo -e "  ${CYAN}[${group_name}]${NC} starting..."

    # Build label args: -L can be given multiple times to match multiple labels
    local label_args=""
    for label in ${labels}; do
        label_args="${label_args} -L ${label}"
    done

    # Run ctest, redirect stdout+stderr to file
    # ${CTEST_ARGS} (e.g. --gtest_filter) is forwarded to each test
    # Note: --output-on-failure must appear only once
    ctest \
        ${label_args} \
        --output-junit "${OUTPUT_DIR}/${group_tag}_junit.xml" \
        ${CTEST_ARGS} \
        --output-on-failure \
        > "${output_file}" 2>&1
    local exit_code=$?

    # Extract failed test names to a separate file (guard against missing log)
    if [ -f "${output_file}" ]; then
        awk '/The following tests FAILED:/{flag=1; next} /^$/{flag=0} flag' "${output_file}" \
            | sed 's/^[[:space:]]*//' \
            | grep -v '^$' \
            | grep -v '^Errors while running' \
            > "${fail_file}" 2>/dev/null || true
    fi

    return ${exit_code}
}

# Launch six test groups in parallel
BASE_PID=""; CORE_PID=""; INTEG_PID=""; HTTP_PID=""; PLATFORM_PID=""; FUZZ_PID=""
BASE_EXIT=0; CORE_EXIT=0; INTEG_EXIT=0; HTTP_EXIT=0; PLATFORM_EXIT=0; FUZZ_EXIT=0

if [ "${HAS_LABEL_FILTER}" = true ]; then
    # User specified a label filter; run ctest directly
    ctest ${CTEST_ARGS} --output-on-failure > "${OUTPUT_DIR}/filtered.log" 2>&1
    FINAL_EXIT=$?
else
    # Launch six test groups in parallel
    run_test_group "base" "Base Test" &
    BASE_PID=$!

    run_test_group "core" "Core Test" &
    CORE_PID=$!

    run_test_group "integration" "Integration Test" &
    INTEG_PID=$!

    run_test_group "http" "HTTP Test" &
    HTTP_PID=$!

    run_test_group "platform" "Platform Test" &
    PLATFORM_PID=$!

    run_test_group "fuzz" "Fuzz Test" &
    FUZZ_PID=$!

    # Wait for all groups
    wait ${BASE_PID};     BASE_EXIT=$?
    wait ${CORE_PID};     CORE_EXIT=$?
    wait ${INTEG_PID};    INTEG_EXIT=$?
    wait ${HTTP_PID};     HTTP_EXIT=$?
    wait ${PLATFORM_PID}; PLATFORM_EXIT=$?
    wait ${FUZZ_PID};     FUZZ_EXIT=$?

    FINAL_EXIT=$((BASE_EXIT | CORE_EXIT | INTEG_EXIT | HTTP_EXIT | PLATFORM_EXIT | FUZZ_EXIT))
fi

# ─── 4. Summary ─────────────────────────────────────────────────────────────────
echo ""
echo -e "${CYAN}============================================${NC}"
echo -e "${CYAN} Test Summary${NC}"
echo -e "${CYAN}============================================${NC}"

ALL_FAILED=()
TOTAL_PASS=0
TOTAL_FAIL=0

collect_results() {
    local group_tag="$1"
    local group_name="$2"
    local output_file="${OUTPUT_DIR}/${group_tag}.log"
    local fail_file="${OUTPUT_DIR}/${group_tag}_failed.log"

    if [ ! -f "${output_file}" ]; then
        return
    fi

    local summary="$(grep -E '^[0-9]+% tests passed' "${output_file}" | tail -1)"
    local total_tests="$(echo "${summary}" | grep -oE 'out of [0-9]+' | grep -oE '[0-9]+')"
    local pass_pct="$(echo "${summary}" | grep -oE '^[0-9]+' | head -1)"
    local fail_count=0

    if [ -z "${total_tests}" ]; then
        total_tests=0
    fi
    if [ -z "${pass_pct}" ]; then
        pass_pct=0
    fi

    if [ -f "${fail_file}" ] && [ -s "${fail_file}" ]; then
        fail_count="$(wc -l < "${fail_file}" | tr -d ' ')"
    fi

    TOTAL_PASS=$((TOTAL_PASS + total_tests - fail_count))
    TOTAL_FAIL=$((TOTAL_FAIL + fail_count))

    if [ "${fail_count}" -gt 0 ]; then
        echo -e " ${RED}✗ ${group_name}${NC}: ${total_tests} tests, ${RED}${fail_count} failed${NC}"
        while IFS= read -r failed_test; do
            ALL_FAILED+=("${group_name} :: ${failed_test}")
        done < "${fail_file}"
    else
        echo -e " ${GREEN}✓ ${group_name}${NC}: ${total_tests} tests all passed"
    fi
}

if [ "${HAS_LABEL_FILTER}" = true ]; then
    SUMMARY=$(grep -E '^[0-9]+% tests passed' "${OUTPUT_DIR}/filtered.log" | tail -1)
    TOTAL_TESTS=$(echo "${SUMMARY}" | grep -oE 'out of [0-9]+' | grep -oE '[0-9]+')
    echo -e " ${GREEN}Result${NC}: ${TOTAL_TESTS:-0} tests"
    FAILED_TESTS=$(awk '/The following tests FAILED:/{flag=1; next} /^$/{flag=0} flag' "${OUTPUT_DIR}/filtered.log" | sed 's/^[[:space:]]*//' | grep -v '^$' | grep -v '^Errors while running')
    if [ -n "${FAILED_TESTS}" ]; then
        echo ""
        echo -e "${RED}────────────────────────────────────────────${NC}"
        echo -e "${RED} FAILED TESTS:${NC}"
        echo -e "${RED}────────────────────────────────────────────${NC}"
        echo "${FAILED_TESTS}" | while read -r line; do
            echo -e "  ${RED}✗${NC} ${line}"
        done
        echo -e "${RED}────────────────────────────────────────────${NC}"
    fi
else
    echo ""
    collect_results "base" "Base Test"
    collect_results "core" "Core Test"
    collect_results "integration" "Integration Test"
    collect_results "http" "HTTP Test"
    collect_results "platform" "Platform Test"
    collect_results "fuzz" "Fuzz Test"

    echo ""
    if [ ${#ALL_FAILED[@]} -eq 0 ]; then
        echo -e " ${GREEN}All ${TOTAL_PASS} tests passed ✓${NC}"
    else
        echo -e " ${GREEN}Passed  : ${TOTAL_PASS}${NC}"
        echo -e " ${RED}Failed  : ${TOTAL_FAIL}${NC}"
        echo ""
        echo -e "${RED}────────────────────────────────────────────${NC}"
        echo -e "${RED} FAILED TESTS:${NC}"
        echo -e "${RED}────────────────────────────────────────────${NC}"
        for failed in "${ALL_FAILED[@]}"; do
            echo -e "  ${RED}✗${NC} ${failed}"
        done
        echo -e "${RED}────────────────────────────────────────────${NC}"
    fi
fi

echo -e "${CYAN}============================================${NC}"
echo " Detailed output: ${OUTPUT_DIR}/"
echo -e "${CYAN}============================================${NC}"

# ─── 5. Clean cache logs ──────────────────────────────────────────────────────
echo ""
echo -e "${YELLOW}Cleaning test cache logs...${NC}"
for cache_dir in "${PROJECT_DIR}/test/base/cache" "${PROJECT_DIR}/test/core/cache" "${PROJECT_DIR}/test/http/cache" "${PROJECT_DIR}/test/platform/cache" "${PROJECT_DIR}/test/fuzz/cache"; do
    if [ -d "${cache_dir}/logs" ]; then
        rm -rf "${cache_dir}/logs"
        echo "  Cleaned: ${cache_dir}/logs/"
    fi
done

exit ${FINAL_EXIT}
