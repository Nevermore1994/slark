#!/bin/bash
# 并行运行所有测试（base / core / integration / http / platform / fuzz）
#
# 输出: 详细结果 → test/output/<YYYYMMDD_HHMMSS>/
#       终端      → 仅打印每组统计 + 最后汇总失败用例
#
# 用法:
#   ./scripts/run_tests.sh              # Debug 构建 + 运行全部测试
#   ./scripts/run_tests.sh Release      # Release 构建
#   ./scripts/run_tests.sh Debug -L base # 只运行 base 标签测试
#   ./scripts/run_tests.sh Debug --gtest_filter="LogConfig*" # 过滤特定测试
#
# 环境变量:
#   BUILD_JOBS=4 ./scripts/run_tests.sh # 指定并行编译任务数

set -o pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"

BUILD_TYPE="${1:-Debug}"
BUILD_DIR="${PROJECT_DIR}/cmake-build-${BUILD_TYPE}-test"
BUILD_JOBS="${BUILD_JOBS:-$(sysctl -n hw.logicalcpu 2>/dev/null || nproc 2>/dev/null || echo 4)}"

# 颜色
GREEN='\033[0;32m'
RED='\033[0;31m'
YELLOW='\033[1;33m'
CYAN='\033[0;36m'
NC='\033[0m'

# 额外参数传递给 ctest（-L、--gtest_filter 等）
shift 2>/dev/null || true
CTEST_ARGS="$@"

# 输出目录
TIMESTAMP="$(date +%Y%m%d_%H%M%S)"
OUTPUT_DIR="${PROJECT_DIR}/test/output/${TIMESTAMP}"
mkdir -p "${OUTPUT_DIR}"

echo -e "${CYAN}============================================${NC}"
echo -e "${CYAN} Slark 测试运行器${NC}"
echo -e "${CYAN}============================================${NC}"
echo " Build Type : ${BUILD_TYPE}"
echo " Build Dir  : ${BUILD_DIR}"
echo " Build Jobs : ${BUILD_JOBS}"
echo " Output Dir : ${OUTPUT_DIR}"
echo " CTest Args : ${CTEST_ARGS:-<none>}"
echo -e "${CYAN}============================================${NC}"

# ─── 1. 配置 ───────────────────────────────────────────────────────────────────
echo ""
echo -e "${YELLOW}[1/3] 配置 CMake...${NC}"
cmake -S "${PROJECT_DIR}" \
      -B "${BUILD_DIR}" \
      -DCMAKE_BUILD_TYPE="${BUILD_TYPE}" \
      -DBUILD_PLATFORM=PC \
      -DDISABLE_HTTP=OFF

# ─── 2. 编译 ───────────────────────────────────────────────────────────────────
echo ""
echo -e "${YELLOW}[2/3] 编译测试...${NC}"
TARGETS="Base_test Core_test http_test Platform_test BaseFuzz_test CoreFuzz_test HttpFuzz_test StringUtilFuzz_test SeekContextFuzz_test TimerPoolFuzz_test FileStreamFuzz_test WriterFuzz_test ThreadFuzz_test"
if [ -d "${PROJECT_DIR}/src/core" ]; then
    TARGETS="${TARGETS} CoreIntegration_test"
fi
# shellcheck disable=SC2086
cmake --build "${BUILD_DIR}" \
      --target ${TARGETS} \
      -j"${BUILD_JOBS}"

# ─── 3. 运行 ───────────────────────────────────────────────────────────────────
echo ""
echo -e "${YELLOW}[3/3] 并行运行测试...${NC}"
cd "${BUILD_DIR}"

# 解析 ctest 参数中的标签过滤
# 如果用户指定了 -L，则只运行对应标签，否则运行全部
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

    echo -e "  ${CYAN}[${group_name}]${NC} 启动..."

    # 构建标签参数：-L 支持多次调用匹配多个标签
    local label_args=""
    for label in ${labels}; do
        label_args="${label_args} -L ${label}"
    done

    # 运行 ctest 并将 stdout+stderr 重定向到文件
    # ${CTEST_ARGS} 中的 --gtest_filter 等参数会透传给每个测试
    # 注意: --output-on-failure 只能出现一次
    ctest \
        ${label_args} \
        --output-junit "${OUTPUT_DIR}/${group_tag}_junit.xml" \
        ${CTEST_ARGS} \
        --output-on-failure \
        > "${output_file}" 2>&1
    local exit_code=$?

    # 提取失败的测试用例名到单独文件
    awk '/The following tests FAILED:/{flag=1; next} /^$/{flag=0} flag' "${output_file}" \
        | sed 's/^[[:space:]]*//' \
        | grep -v '^$' \
        | grep -v '^Errors while running' \
        > "${fail_file}" 2>/dev/null || true

    return ${exit_code}
}

# 并行启动六个测试组
BASE_PID=""; CORE_PID=""; INTEG_PID=""; HTTP_PID=""; PLATFORM_PID=""; FUZZ_PID=""
BASE_EXIT=0; CORE_EXIT=0; INTEG_EXIT=0; HTTP_EXIT=0; PLATFORM_EXIT=0; FUZZ_EXIT=0

if [ "${HAS_LABEL_FILTER}" = true ]; then
    # 用户指定了标签过滤，直接运行 ctest
    ctest ${CTEST_ARGS} --output-on-failure > "${OUTPUT_DIR}/filtered.log" 2>&1
    FINAL_EXIT=$?
else
    # 并行启动六个测试组（base / core / integration / http / platform / fuzz）
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

    # 等待所有测试完成
    wait ${BASE_PID};     BASE_EXIT=$?
    wait ${CORE_PID};     CORE_EXIT=$?
    wait ${INTEG_PID};    INTEG_EXIT=$?
    wait ${HTTP_PID};     HTTP_EXIT=$?
    wait ${PLATFORM_PID}; PLATFORM_EXIT=$?
    wait ${FUZZ_PID};     FUZZ_EXIT=$?

    FINAL_EXIT=$((BASE_EXIT | CORE_EXIT | INTEG_EXIT | HTTP_EXIT | PLATFORM_EXIT | FUZZ_EXIT))
fi

# ─── 4. 汇总 ───────────────────────────────────────────────────────────────────
echo ""
echo -e "${CYAN}============================================${NC}"
echo -e "${CYAN} 测试汇总${NC}"
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
        echo -e " ${RED}✗ ${group_name}${NC}: ${total_tests} 个测试, ${RED}${fail_count} 个失败${NC}"
        while IFS= read -r failed_test; do
            ALL_FAILED+=("${group_name} :: ${failed_test}")
        done < "${fail_file}"
    else
        echo -e " ${GREEN}✓ ${group_name}${NC}: ${total_tests} 个测试全部通过"
    fi
}

if [ "${HAS_LABEL_FILTER}" = true ]; then
    SUMMARY=$(grep -E '^[0-9]+% tests passed' "${OUTPUT_DIR}/filtered.log" | tail -1)
    TOTAL_TESTS=$(echo "${SUMMARY}" | grep -oE 'out of [0-9]+' | grep -oE '[0-9]+')
    echo -e " ${GREEN}结果${NC}: ${TOTAL_TESTS:-0} 个测试"
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
        echo -e " ${GREEN}全部 ${TOTAL_PASS} 个测试通过 ✓${NC}"
    else
        echo -e " ${GREEN}通过  : ${TOTAL_PASS}${NC}"
        echo -e " ${RED}失败  : ${TOTAL_FAIL}${NC}"
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
echo " 详细输出: ${OUTPUT_DIR}/"
echo -e "${CYAN}============================================${NC}"

# ─── 5. 清理 cache log ──────────────────────────────────────────────────────
echo ""
echo -e "${YELLOW}清理测试 cache log...${NC}"
for cache_dir in "${PROJECT_DIR}/test/base/cache" "${PROJECT_DIR}/test/core/cache" "${PROJECT_DIR}/test/http/cache" "${PROJECT_DIR}/test/platform/cache" "${PROJECT_DIR}/test/fuzz/cache"; do
    if [ -d "${cache_dir}/logs" ]; then
        rm -rf "${cache_dir}/logs"
        echo "  已清理: ${cache_dir}/logs/"
    fi
done

exit ${FINAL_EXIT}
