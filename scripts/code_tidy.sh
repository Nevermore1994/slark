#!/bin/bash
# =============================================================================
# Slark Code Tidy Script
# 静态分析（clang-tidy）
# =============================================================================

set -e

# 设置默认 TERM 环境变量（CI 环境可能没有设置）
if [ -z "${TERM}" ]; then
    export TERM="dumb"
fi

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(dirname "${SCRIPT_DIR}")"

# 自动查找可用的 clang-tidy（优先使用环境变量，其次按版本从新到旧查找）
if [ -z "${CLANG_TIDY}" ]; then
    for candidate in clang-tidy clang-tidy-19 clang-tidy-18 clang-tidy-17 clang-tidy-16; do
        if command -v "${candidate}" &>/dev/null; then
            CLANG_TIDY="$(command -v "${candidate}")"
            break
        fi
    done
fi
CLANG_TIDY="${CLANG_TIDY:-clang-tidy}"

# 默认配置
VERBOSE="${VERBOSE:-ON}"
SHOW_FULL_PATH="${SHOW_FULL_PATH:-OFF}"

# 源代码目录
SOURCE_DIRS=(
    "${PROJECT_DIR}/src"
)

# 排除的目录模式（基础排除，不受编译配置影响）
EXCLUDE_PATTERNS=(
    "*/thirdParty/*"
    "*/build/*"
    "*/.fetchcontent_cache/*"
    "*/test/*"
)

# 颜色输出
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

print_info() {
    echo -e "${GREEN}[INFO]${NC} $1"
}

print_warn() {
    echo -e "${YELLOW}[WARN]${NC} $1"
}

print_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

print_detail() {
    echo -e "${BLUE}[DETAIL]${NC} $1"
}

# 格式化文件路径输出
format_file_path() {
    local file="$1"
    if [ "${SHOW_FULL_PATH}" = "ON" ]; then
        echo "${file}"
    else
        echo "$(basename "${file}") (in: $(dirname "${file}"))"
    fi
}

usage() {
    cat <<EOF
Usage: $(basename "$0") [options]

Slark Code Tidy Script - 静态分析

Options:
  --verbose          详细输出
  --brief            简要输出（仅显示失败文件，默认）
  --short-path       显示短路径（仅文件名+目录，默认）
  --full-path        显示完整路径
  --help             显示帮助信息

Examples:
  # 运行静态分析
  $(basename "$0")

  # 简要输出
  $(basename "$0") --brief
EOF
    exit 0
}

# 解析命令行参数
while [[ $# -gt 0 ]]; do
    case "$1" in
        --verbose)
            VERBOSE="ON"
            shift
            ;;
        --brief)
            VERBOSE="OFF"
            shift
            ;;
        --short-path)
            SHOW_FULL_PATH="OFF"
            shift
            ;;
        --full-path)
            SHOW_FULL_PATH="ON"
            shift
            ;;
        --help|-h)
            usage
            ;;
        *)
            print_error "Unknown option: $1"
            usage
            ;;
    esac
done

# 收集源文件（结果存入全局数组 SOURCE_FILES）
SOURCE_FILES=()
collect_source_files() {
    SOURCE_FILES=()

    for dir in "${SOURCE_DIRS[@]}"; do
        if [ -d "${dir}" ]; then
            while IFS= read -r -d '' file; do
                local exclude=0
                for pattern in "${EXCLUDE_PATTERNS[@]}"; do
                    if [[ "${file}" == ${pattern} ]]; then
                        exclude=1
                        break
                    fi
                done

                if [ "${exclude}" -eq 0 ]; then
                    SOURCE_FILES+=("${file}")
                fi
            done < <(find "${dir}" -type f \( -name "*.cpp" -o -name "*.cc" -o -name "*.h" -o -name "*.hpp" -o -name "*.mm" -o -name "*.m" \) -print0 2>/dev/null)
        fi
    done
}

# 检查 clang-tidy 是否可用
check_clang_tidy() {
    if [ ! -x "${CLANG_TIDY}" ]; then
        print_error "clang-tidy not found. Please install clang-tidy (e.g. brew install llvm or apt install clang-tidy)."
        exit 1
    fi
    print_info "Using clang-tidy: ${CLANG_TIDY} ($(${CLANG_TIDY} --version | head -1))"
}

# 静态分析
run_tidy_check() {
    print_info "========================================"
    print_info "Starting static analysis (clang-tidy)..."
    print_info "========================================"
    
    check_clang_tidy
    
    local compile_db="${PROJECT_DIR}/compile_commands.json"
    if [ ! -f "${compile_db}" ]; then
        compile_db="${PROJECT_DIR}/build/compile_commands.json"
    fi
    
    if [ ! -f "${compile_db}" ]; then
        print_warn "compile_commands.json not found at: ${compile_db}"
        print_warn "Static analysis may be incomplete or inaccurate."
        print_warn "Please build the project with CMAKE_EXPORT_COMPILE_COMMANDS=ON"
        echo ""
    else
        print_info "Using compile_commands.json from: $(dirname "${compile_db}")"
    fi
    
    collect_source_files
    local files=("${SOURCE_FILES[@]}")

    if [ ${#files[@]} -eq 0 ]; then
        print_warn "No source files found."
        return 0
    fi
    
    # 筛选出C++源文件（仅.cpp/.cc）
    local cpp_files=()
    for file in "${files[@]}"; do
        if [[ "${file}" == *.cpp ]] || [[ "${file}" == *.cc ]] || [[ "${file}" == *.mm ]]; then
            cpp_files+=("${file}")
        fi
    done
    
    if [ ${#cpp_files[@]} -eq 0 ]; then
        print_warn "No C++ source files (.cpp/.cc) found for static analysis."
        return 0
    fi
    
    print_info "Found ${#cpp_files[@]} C++ source files to analyze."
    echo ""
    
    # 并行任务数（clang-tidy内存和CPU消耗大，并发数为CPU核心数的一半，避免竞争）
    local jobs=$(( $(nproc 2>/dev/null || echo 4) / 2 ))
    if [ "${jobs}" -lt 1 ]; then
        jobs=1
    fi
    if [ "${jobs}" -gt 16 ]; then
        jobs=16
    fi
    # 单文件超时时间（秒）
    local per_file_timeout=300
    print_info "Using ${jobs} parallel jobs for analysis (timeout: ${per_file_timeout}s per file)..."
    echo ""
    
    # clang-tidy 参数配置
    # header-filter 设为空字符串，只检查源文件本身，不检查头文件中的问题（大幅提升速度）
    local tidy_args=(
        --checks="bugprone-*,-bugprone-unchecked-optional-access,security-*,cppcoreguidelines-*,-cppcoreguidelines-avoid-do-while,-cppcoreguidelines-avoid-magic-numbers,-cppcoreguidelines-special-member-functions,-cppcoreguidelines-macro-usage,-cppcoreguidelines-avoid-const-or-ref-data-members,modernize-*,-modernize-use-trailing-return-type,performance-*,readability-*,-readability-implicit-bool-conversion,-readability-function-cognitive-complexity,-readability-magic-numbers,-readability-identifier-length"
        --header-filter=""
        --quiet
    )
    
    if [ -f "${compile_db}" ]; then
        tidy_args+=( -p "$(dirname "${compile_db}")" )
    fi
    
    # 创建日志目录（在build目录下）
    local log_dir="${PROJECT_DIR}/build/code_scan"
    mkdir -p "${log_dir}"
    
    # 清空旧的日志文件
    rm -f "${log_dir}"/*.log 2>/dev/null
    
    # 初始化结果数组
    local failed_files=()
    local timeout_files=()
    local ok_files=()
    
    # 使用更简单可靠的并行执行方式
    local total_files=${#cpp_files[@]}
    local pids=()
    local file_map=()
    
    # 清理函数：杀死所有子进程
    cleanup_children() {
        for pid in "${pids[@]}"; do
            kill "${pid}" 2>/dev/null
        done
    }
    trap cleanup_children EXIT INT TERM
    
    # 构建clang-tidy参数数组（避免使用eval）
    local tidy_base_args=("${tidy_args[@]}")
    
    print_info "Starting parallel analysis... (Total: ${total_files} files, Jobs: ${jobs})"
    echo ""
    
    # 启动所有进程（控制并发数）
    local file_index=0
    for file in "${cpp_files[@]}"; do
        # 等待有可用槽位
        while [ ${#pids[@]} -ge ${jobs} ]; do
            # 等待任意一个进程完成
            local new_pids=()
            local new_file_map=()
            for i in "${!pids[@]}"; do
                if kill -0 "${pids[$i]}" 2>/dev/null; then
                    new_pids+=("${pids[$i]}")
                    new_file_map+=("${file_map[$i]}")
                else
                    # 进程已完成，收集结果
                    wait "${pids[$i]}" 2>/dev/null
                    local exit_code=$?
                    local check_file="${file_map[$i]}"
                    local log_file="${log_dir}/$(basename "${check_file}").log"
                    
                    if [ "${exit_code}" -eq 124 ]; then
                        timeout_files+=("${check_file}")
                        print_warn "⏱️  Timeout: $(format_file_path "${check_file}")"
                    elif [ "${exit_code}" -ne 0 ]; then
                        failed_files+=("${check_file}")
                        print_error "❌ Issues: $(format_file_path "${check_file}")"
                    else
                        if [ -f "${log_file}" ] && grep -qE "^(warning|error):" "${log_file}" 2>/dev/null; then
                            failed_files+=("${check_file}")
                            print_error "❌ Warnings: $(format_file_path "${check_file}")"
                        else
                            ok_files+=("${check_file}")
                            if [ "${VERBOSE}" = "ON" ]; then
                                print_detail "✅ OK: $(format_file_path "${check_file}")"
                            fi
                        fi
                    fi
                fi
            done
            pids=("${new_pids[@]}")
            file_map=("${new_file_map[@]}")
            [ ${#pids[@]} -ge ${jobs} ] && sleep 0.2
        done
        
        # 启动新进程
        local log_file="${log_dir}/$(basename "${file}").log"
        timeout "${per_file_timeout}" "${CLANG_TIDY}" "${tidy_base_args[@]}" "${file}" > "${log_file}" 2>&1 &
        pids+=($!)
        file_map+=("${file}")
        file_index=$((file_index + 1))
        
        printf "\r${BLUE}[PROGRESS]${NC} Starting: %d/%d files...    " "${file_index}" "${total_files}"
    done
    
    echo ""
    print_info "All processes started, waiting for completion..."
    
    # 等待所有进程完成
    local remaining=${#pids[@]}
    while [ "${remaining}" -gt 0 ]; do
        local new_pids=()
        local new_file_map=()
        for i in "${!pids[@]}"; do
            if kill -0 "${pids[$i]}" 2>/dev/null; then
                new_pids+=("${pids[$i]}")
                new_file_map+=("${file_map[$i]}")
            else
                # 进程已完成，收集结果
                wait "${pids[$i]}" 2>/dev/null
                local exit_code=$?
                local check_file="${file_map[$i]}"
                local log_file="${log_dir}/$(basename "${check_file}").log"
                
                if [ "${exit_code}" -eq 124 ]; then
                    timeout_files+=("${check_file}")
                    print_warn "⏱️  Timeout: $(format_file_path "${check_file}")"
                elif [ "${exit_code}" -ne 0 ]; then
                    failed_files+=("${check_file}")
                    print_error "❌ Issues: $(format_file_path "${check_file}")"
                else
                    if [ -f "${log_file}" ] && grep -qE "^(warning|error):" "${log_file}" 2>/dev/null; then
                        failed_files+=("${check_file}")
                        print_error "❌ Warnings: $(format_file_path "${check_file}")"
                    else
                        ok_files+=("${check_file}")
                        if [ "${VERBOSE}" = "ON" ]; then
                            print_detail "✅ OK: $(format_file_path "${check_file}")"
                        fi
                    fi
                fi
            fi
        done
        pids=("${new_pids[@]}")
        file_map=("${new_file_map[@]}")
        remaining=${#pids[@]}
        
        if [ "${remaining}" -gt 0 ]; then
            printf "\r${BLUE}[PROGRESS]${NC} Remaining: %d files...    " "${remaining}"
            sleep 0.3
        fi
    done
    echo ""
    
    # 清理trap（不删除日志目录）
    trap - EXIT INT TERM
    
    # 详细输出每个文件的结果
    print_info "========================================"
    print_info "Static Analysis Detailed Results:"
    print_info "========================================"
    echo ""
    
    # 显示失败的文件（总是显示）
    if [ ${#failed_files[@]} -gt 0 ]; then
        print_error "❌ Files with issues (${#failed_files[@]}):"
        for file in "${failed_files[@]}"; do
            local file_path=$(format_file_path "${file}")
            local log_file="${log_dir}/$(basename "${file}").log"
            
            print_error "----------------------------------------"
            print_error "File: ${file_path}"
            print_error "Issues found:"
            echo ""
            
            # 显示具体的警告/错误
            if [ -f "${log_file}" ]; then
                # 显示所有非空行
                grep -v "^$" "${log_file}" 2>/dev/null | head -50 | while read -r line; do
                    echo -e "   ${RED}${line}${NC}"
                done
                # 如果行数超过50，显示省略提示
                local line_count=$(grep -c -v "^$" "${log_file}" 2>/dev/null)
                line_count=$(echo "${line_count}" | head -1 | grep -o '[0-9]*' | head -1)
                line_count=${line_count:-0}
                if [ "${line_count}" -gt 50 ]; then
                    echo -e "   ${YELLOW}... (${line_count} lines total, showing first 50)${NC}"
                    echo -e "   ${YELLOW}See full log: ${log_file}${NC}"
                fi
            else
                print_error "   (log file not found: ${log_file})"
            fi
            echo ""
        done
        echo ""
    fi
    
    # 显示超时的文件
    if [ ${#timeout_files[@]} -gt 0 ]; then
        print_warn "⏱️ Files timed out (${#timeout_files[@]}):"
        for file in "${timeout_files[@]}"; do
            print_warn "   - $(format_file_path "${file}") (timeout after ${per_file_timeout}s)"
        done
        echo ""
    fi
    
    # 显示成功的文件（仅在详细模式下）
    if [ "${VERBOSE}" = "ON" ] && [ ${#ok_files[@]} -gt 0 ]; then
        print_detail "✅ Files without issues (${#ok_files[@]}):"
        for file in "${ok_files[@]}"; do
            print_detail "   - $(format_file_path "${file}")"
        done
        echo ""
    fi
    
    # 输出汇总
    print_info "========================================"
    print_info "Static Analysis Summary:"
    print_info "========================================"
    print_info "📊 Total files analyzed: ${total_files}"
    print_info "✅ No issues: ${#ok_files[@]}"
    if [ ${#failed_files[@]} -gt 0 ]; then
        print_error "❌ With issues: ${#failed_files[@]}"
    else
        print_info "❌ With issues: 0"
    fi
    if [ ${#timeout_files[@]} -gt 0 ]; then
        print_warn "⏱️ Timed out: ${#timeout_files[@]}"
    else
        print_info "⏱️ Timed out: 0"
    fi
    echo ""
    
    # 日志保存位置提示
    print_info "📁 Log files saved to: ${log_dir}"
    echo ""
    
    # 返回结果
    if [ ${#failed_files[@]} -gt 0 ] || [ ${#timeout_files[@]} -gt 0 ]; then
        print_error "❌ Static analysis completed with issues/timeout!"
        return 1
    else
        print_info "🎉 Static analysis passed for all files!"
        return 0
    fi
}

# 主函数
main() {
    echo "=========================================="
    echo -e "${BLUE}Slark Code Tidy (Static Analysis)${NC}"
    echo "=========================================="
    echo "  Project Directory : ${PROJECT_DIR}"
    echo "  Verbose Output    : ${VERBOSE}"
    echo "  File Path Display : ${SHOW_FULL_PATH}"
    echo "=========================================="
    echo ""
    
    local exit_code=0
    
    if ! run_tidy_check; then
        exit_code=1
    fi
    
    echo ""
    echo "=========================================="
    if [ "${exit_code}" -eq 0 ]; then
        print_info "🎉 Static analysis passed successfully!"
    else
        print_error "❌ Static analysis failed - please fix the issues above!"
    fi
    echo "=========================================="
    
    exit ${exit_code}
}

main
