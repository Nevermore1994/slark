#!/bin/bash
# Run all tests in parallel (base / core / integration / http / platform / fuzz)
#
# Output: detailed results -> test/output/<YYYYMMDD_HHMMSS>/
#         terminal         -> aggregate stats + final failed-test summary
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

# Extra args passed to ctest. `--gtest_filter` is translated to CTest regex
# because gtest_discover_tests executes one discovered test per CTest entry.
shift || true
CTEST_ARGS=()
GTEST_FILTER=""
while (($# > 0)); do
    case "$1" in
        --gtest_filter=*)
            GTEST_FILTER="${1#*=}"
            shift
            ;;
        --gtest_filter)
            if (($# < 2)); then
                echo "--gtest_filter requires a value" >&2
                exit 2
            fi
            GTEST_FILTER="$2"
            shift 2
            ;;
        *)
            CTEST_ARGS+=("$1")
            shift
            ;;
    esac
done

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
if ((${#CTEST_ARGS[@]} > 0)); then
    CTEST_ARGS_DISPLAY="${CTEST_ARGS[*]}"
else
    CTEST_ARGS_DISPLAY="<none>"
fi
echo " CTest Args : ${CTEST_ARGS_DISPLAY}"
echo -e "${CYAN}============================================${NC}"

# ─── 1. Configure ───────────────────────────────────────────────────────────────
echo ""
echo -e "${YELLOW}[1/3] Configuring CMake...${NC}"
if ! cmake -S "${PROJECT_DIR}" \
      -B "${BUILD_DIR}" \
      -DCMAKE_BUILD_TYPE="${BUILD_TYPE}" \
      -DBUILD_PLATFORM=PC \
      -DDISABLE_HTTP=OFF; then
    echo -e "${RED}CMake configure failed${NC}" >&2
    exit 1
fi

# ─── 2. Build ───────────────────────────────────────────────────────────────────
echo ""
echo -e "${YELLOW}[2/3] Building tests...${NC}"
TARGETS="Base_test Core_test http_test Platform_test CoreFuzz_test HttpFuzz_test StringUtilFuzz_test SeekContextFuzz_test TimerPoolFuzz_test FileStreamFuzz_test WriterFuzz_test ThreadFuzz_test"
if [ -d "${PROJECT_DIR}/src/core" ]; then
    TARGETS="${TARGETS} CoreIntegration_test PlayerMediaIntegration_test Engine_test"
fi
# shellcheck disable=SC2086
if ! cmake --build "${BUILD_DIR}" \
      --target ${TARGETS} \
      -j"${BUILD_JOBS}"; then
    echo -e "${RED}Test target build failed${NC}" >&2
    exit 1
fi

# ─── 3. Run ─────────────────────────────────────────────────────────────────────
echo ""
echo -e "${YELLOW}[3/3] Running tests in parallel...${NC}"
cd "${BUILD_DIR}"

TEST_JOBS="${TEST_JOBS:-${BUILD_JOBS}}"
TEST_LOG="${OUTPUT_DIR}/ctest.log"
TEST_JUNIT="${OUTPUT_DIR}/ctest_junit.xml"
CTEST_RUN_ARGS=(
    --output-on-failure
    --output-junit "${TEST_JUNIT}"
    --no-tests=error
    --parallel "${TEST_JOBS}"
)

# A single CTest process is intentional. Running several CTest processes against
# one build tree races while gtest_discover_tests regenerates *_tests.cmake.
# CTest itself still runs individual discovered tests in parallel.
if [[ -n "${GTEST_FILTER}" ]]; then
    glob_to_regex() {
        printf '%s' "$1" | sed -e 's/[.[\\^$()+{}|]/\\&/g' -e 's/\*/.*/g' -e 's/?/./g'
    }

    positive_filter="${GTEST_FILTER%%-*}"
    negative_filter=""
    if [[ "${GTEST_FILTER}" == *-* ]]; then
        negative_filter="${GTEST_FILTER#*-}"
    fi

    if [[ -n "${positive_filter}" && "${positive_filter}" != "*" ]]; then
        IFS=':' read -r -a positive_patterns <<< "${positive_filter}"
        positive_regex=""
        for pattern in "${positive_patterns[@]}"; do
            [[ -z "${pattern}" ]] && continue
            pattern_regex="$(glob_to_regex "${pattern}")"
            if [[ -n "${positive_regex}" ]]; then
                positive_regex+="|"
            fi
            positive_regex+="${pattern_regex}"
        done
        [[ -n "${positive_regex}" ]] && CTEST_RUN_ARGS+=(--tests-regex "^(${positive_regex})$")
    fi

    if [[ -n "${negative_filter}" ]]; then
        IFS=':' read -r -a negative_patterns <<< "${negative_filter}"
        negative_regex=""
        for pattern in "${negative_patterns[@]}"; do
            [[ -z "${pattern}" ]] && continue
            pattern_regex="$(glob_to_regex "${pattern}")"
            if [[ -n "${negative_regex}" ]]; then
                negative_regex+="|"
            fi
            negative_regex+="${pattern_regex}"
        done
        [[ -n "${negative_regex}" ]] && CTEST_RUN_ARGS+=(-E "^(${negative_regex})$")
    fi
fi

CTEST_RUN_ARGS+=("${CTEST_ARGS[@]}")
ctest "${CTEST_RUN_ARGS[@]}" >"${TEST_LOG}" 2>&1
FINAL_EXIT=$?

awk '/The following tests FAILED:/{flag=1; next} flag && NF {print}' "${TEST_LOG}" \
    | sed 's/^[[:space:]]*//' \
    | grep -v '^Errors while running' \
    > "${OUTPUT_DIR}/failed_tests.log" 2>/dev/null || true

# ─── 4. Summary ─────────────────────────────────────────────────────────────────
echo ""
echo -e "${CYAN}============================================${NC}"
echo -e "${CYAN} Test Summary${NC}"
echo -e "${CYAN}============================================${NC}"

SUMMARY_LINE="$(grep -E '[0-9]+% tests passed, [0-9]+ tests failed out of [0-9]+' "${TEST_LOG}" | tail -1)"
if [[ -n "${SUMMARY_LINE}" ]]; then
    if [[ "${SUMMARY_LINE}" =~ ([0-9]+)%\ tests\ passed,\ ([0-9]+)\ tests\ failed\ out\ of\ ([0-9]+) ]]; then
        PASS_COUNT="${BASH_REMATCH[1]}"
        TOTAL_FAIL="${BASH_REMATCH[2]}"
        TOTAL_TESTS="${BASH_REMATCH[3]}"
        TOTAL_PASS=$((TOTAL_TESTS - TOTAL_FAIL))
    else
        echo -e " ${RED}✗ Unable to parse CTest summary: ${SUMMARY_LINE}${NC}"
        FINAL_EXIT=1
        TOTAL_PASS=0
        TOTAL_FAIL=0
        TOTAL_TESTS=0
    fi
else
    echo -e " ${RED}✗ Test discovery/execution failed; no valid CTest summary was produced${NC}"
    if grep -Eq 'CMake Error|Errors while running CTest|No tests were found' "${TEST_LOG}"; then
        echo -e " ${RED}  See ${TEST_LOG} for the discovery error${NC}"
    fi
    FINAL_EXIT=1
    TOTAL_PASS=0
    TOTAL_FAIL=0
    TOTAL_TESTS=0
fi

if [[ "${FINAL_EXIT}" -eq 0 && "${TOTAL_TESTS}" -gt 0 && "${TOTAL_FAIL}" -eq 0 ]]; then
    echo -e " ${GREEN}All ${TOTAL_PASS} tests passed ✓${NC}"
elif [[ "${TOTAL_TESTS}" -gt 0 ]]; then
    echo -e " ${GREEN}Passed  : ${TOTAL_PASS}${NC}"
    echo -e " ${RED}Failed  : ${TOTAL_FAIL}${NC}"
else
    echo -e " ${RED}No tests were counted; the run is considered failed${NC}"
fi

if [[ -s "${OUTPUT_DIR}/failed_tests.log" ]]; then
    echo ""
    echo -e "${RED} FAILED TESTS:${NC}"
    while IFS= read -r failed_test; do
        echo -e "  ${RED}✗${NC} ${failed_test}"
    done < "${OUTPUT_DIR}/failed_tests.log"
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
