#!/bin/bash
set -e

# Slark Coverage Generation Script
# This script runs tests and collects coverage data using gcovr.

SLARK_ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

# 覆盖率测试配置
BUILD_TYPE="Debug"
BUILD_DIR="${SLARK_ROOT_DIR}/cmake-build-${BUILD_TYPE}"
COVERAGE_DIR="${SLARK_ROOT_DIR}/coverage"
LOG_FILE="${COVERAGE_DIR}/coverage_summary.log"
TMP_LOG_FILE="${LOG_FILE}.tmp"

# 源代码目录
SOURCE_DIRS=("src")

echo "===================================================="
echo "Slark Coverage Generation Tool"
echo "Build Dir:  $BUILD_DIR"
echo "Source:     ${SOURCE_DIRS[*]}"
echo "===================================================="

# 1. 检查构建目录是否存在
if [ ! -d "$BUILD_DIR" ]; then
    echo "Error: Build directory $BUILD_DIR not found."
    echo "Please run build.sh first to generate the project."
    exit 1
fi

# 2. 清理旧的覆盖率数据
echo "[1/3] Cleaning up old coverage data in $BUILD_DIR..."
find "$BUILD_DIR" -name "*.gcda" -delete
mkdir -p "${COVERAGE_DIR}"
rm -f "${TMP_LOG_FILE}"

# 3. 运行测试
echo "[2/3] Running tests..."

# 检查是否有测试可执行文件
TEST_BINARIES=()
if [ -d "${BUILD_DIR}/bin" ]; then
    while IFS= read -r -d '' test_bin; do
        if [ -x "$test_bin" ] && [[ "$(basename "$test_bin")" == *test* ]]; then
            TEST_BINARIES+=("$test_bin")
        fi
    done < <(find "${BUILD_DIR}/bin" -type f -print0 2>/dev/null)
fi

# 如果没有找到测试，尝试运行 ctest
if [ ${#TEST_BINARIES[@]} -eq 0 ]; then
    echo "No test binaries found in ${BUILD_DIR}/bin"
    echo "Trying to run tests via ctest..."
    
    if command -v ctest &>/dev/null; then
        cd "${BUILD_DIR}"
        ctest --output-on-failure || {
            echo "Warning: Some tests failed, but coverage data will still be collected."
        }
    else
        echo "Warning: ctest not found, skipping test execution."
    fi
else
    echo "Found ${#TEST_BINARIES[@]} test binary(ies)"
    export LD_LIBRARY_PATH=$LD_LIBRARY_PATH:${BUILD_DIR}/lib
    
    FAILED_TESTS=()
    for test_bin in "${TEST_BINARIES[@]}"; do
        test_name=$(basename "$test_bin")
        echo "Running: $test_name"
        
        if ! "$test_bin" > "${COVERAGE_DIR}/${test_name}.log" 2>&1; then
            FAILED_TESTS+=("$test_name")
            echo "  Failed: $test_name"
        else
            echo "  Passed: $test_name"
        fi
    done
    
    if [ ${#FAILED_TESTS[@]} -gt 0 ]; then
        echo "Warning: Some tests failed: ${FAILED_TESTS[*]}"
        echo "Coverage data will still be collected."
    fi
fi

# 4. 生成覆盖率报告
echo "[3/3] Collecting coverage data..."

GCOVR_FILTER_ARGS=""
for dir in "${SOURCE_DIRS[@]}"; do
    GCOVR_FILTER_ARGS="${GCOVR_FILTER_ARGS} --filter ${SLARK_ROOT_DIR}/${dir}"
done

# 检查 gcovr 是否可用
if ! command -v gcovr &>/dev/null; then
    echo "Error: gcovr not found. Please install gcovr:"
    echo "  pip install gcovr"
    exit 1
fi

gcovr -r "${SLARK_ROOT_DIR}" \
    "${BUILD_DIR}" \
    ${GCOVR_FILTER_ARGS} \
    --exclude ".*/test/.*" \
    --exclude ".*/thirdParty/.*" \
    --exclude ".*/\\.fetchcontent_cache/.*" \
    --exclude "/usr/.*" \
    --merge-mode-functions=merge-use-line-0 \
    --exclude-throw-branches \
    --exclude-unreachable-branches \
    --exclude-noncode-lines \
    --gcov-ignore-errors all \
    --gcov-ignore-parse-errors suspicious_hits.warn \
    --txt-metric branch \
    --print-summary \
    --sort uncovered-number > "${TMP_LOG_FILE}" 2>/dev/null || {
        echo "Warning: gcovr encountered some errors, but may have produced valid output."
    }

if [ -s "${TMP_LOG_FILE}" ]; then
    mv "${TMP_LOG_FILE}" "${LOG_FILE}"
else
    echo "Error: gcovr did not generate valid output."
    echo "Possible causes:"
    echo "  1. No coverage data (.gcda files) found - ensure tests were compiled with coverage flags"
    echo "  2. No source files matched the filter criteria"
    echo ""
    echo "To enable coverage, rebuild with:"
    echo "  cmake -B build -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_FLAGS='--coverage' -DCMAKE_EXE_LINKER_FLAGS='--coverage'"
    exit 1
fi

if [ ! -f "${LOG_FILE}" ]; then
    echo "Error: Coverage summary file not found: ${LOG_FILE}"
    exit 1
fi

echo ""
echo "===================================================="
echo "Coverage Summary:"
echo "===================================================="
grep "TOTAL" "${LOG_FILE}" || head -n 20 "${LOG_FILE}"
echo "===================================================="
echo ""
echo "Coverage report saved to: ${LOG_FILE}"
