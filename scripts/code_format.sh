#!/bin/bash
# =============================================================================
# Slark Code Format Script
# 代码格式检查（clang-format + ktlint）
# =============================================================================

set -e

# 设置默认 TERM 环境变量（CI 环境可能没有设置）
if [ -z "${TERM}" ]; then
    export TERM="dumb"
fi

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(dirname "${SCRIPT_DIR}")"

# 自动查找可用的 clang-format（优先使用环境变量，其次按版本从新到旧查找）
if [ -z "${CLANG_FORMAT}" ]; then
    for candidate in clang-format clang-format-19 clang-format-18 clang-format-17 clang-format-16; do
        if command -v "${candidate}" &>/dev/null; then
            CLANG_FORMAT="$(command -v "${candidate}")"
            break
        fi
    done
fi
CLANG_FORMAT="${CLANG_FORMAT:-clang-format}"

# 自动查找可用的 ktlint
KTLINT="${KTLINT:-ktlint}"

# 默认配置
FIX_FORMAT="${FIX_FORMAT:-OFF}"
VERBOSE="${VERBOSE:-ON}"
SHOW_FULL_PATH="${SHOW_FULL_PATH:-OFF}"

# 源代码目录
SOURCE_DIRS=(
    "${PROJECT_DIR}/src"
    "${PROJECT_DIR}/test"
    "${PROJECT_DIR}/include"
    "${PROJECT_DIR}/internal"
)

# 排除的目录模式
EXCLUDE_PATTERNS=(
    "*/thirdParty/*"
    "*/build/*"
    "*/.cxx/*"
    "*/.fetchcontent_cache/*"
    "*/Masonry/*"
    "*/EXTScope/*"
    "*/cmake-build-*/*"
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

Slark Code Format Script - 代码格式检查

Options:
  --fix              自动修复代码风格问题
  --verbose          详细输出
  --brief            简要输出（仅显示失败文件，默认）
  --short-path       显示短路径（仅文件名+目录，默认）
  --full-path        显示完整路径
  --help             显示帮助信息

Examples:
  # 检查代码风格
  $(basename "$0")

  # 自动修复代码风格问题
  $(basename "$0") --fix

  # 简要输出
  $(basename "$0") --brief
EOF
    exit 0
}

# 解析命令行参数
while [[ $# -gt 0 ]]; do
    case "$1" in
        --fix)
            FIX_FORMAT="ON"
            shift
            ;;
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

# 检查 clang-format 是否可用
check_clang_format() {
    if [ ! -x "${CLANG_FORMAT}" ]; then
        print_error "clang-format not found. Please install clang-format (e.g. brew install clang-format or apt install clang-format)."
        exit 1
    fi
    print_info "Using clang-format: ${CLANG_FORMAT} ($(${CLANG_FORMAT} --version | head -1))"
}

# 代码风格检查
run_format_check() {
    check_clang_format
    collect_source_files
    local files=("${SOURCE_FILES[@]}")

    if [ ${#files[@]} -eq 0 ]; then
        print_warn "No source files found."
        return 0
    fi

    # --fix 模式：先批量格式化，再验证
    if [ "${FIX_FORMAT}" = "ON" ]; then
        print_info "========================================"
        print_info "Fixing code format (${#files[@]} files)..."
        print_info "========================================"
        "${CLANG_FORMAT}" -i "${files[@]}"
        echo ""
    else
        print_info "========================================"
        print_info "Checking code format (${#files[@]} files)..."
        print_info "========================================"
        echo ""
    fi

    # 统一用 check 模式验证
    local format_issues=()
    for file in "${files[@]}"; do
        local file_path=$(format_file_path "${file}")
        if "${CLANG_FORMAT}" --dry-run --Werror "${file}" 2>/tmp/clang_format_err_$$; then
            if [ "${VERBOSE}" = "ON" ]; then
                print_detail "✅ Format OK: ${file_path}"
            fi
        else
            format_issues+=("${file}")
            print_error "❌ Format issue: ${file_path}"
            print_error "   Error details:"
            cat /tmp/clang_format_err_$$ | sed 's/^/   /'
            echo ""
        fi
    done
    rm -f /tmp/clang_format_err_$$

    # 输出汇总
    echo ""
    print_info "========================================"
    print_info "Code Format Summary:"
    print_info "========================================"
    print_info "✅ Format OK: $(( ${#files[@]} - ${#format_issues[@]} )) files"

    if [ ${#format_issues[@]} -gt 0 ]; then
        print_error "❌ Format issues: ${#format_issues[@]} files"
        for file in "${format_issues[@]}"; do
            echo "   - $(format_file_path "${file}")"
        done
        echo ""
        if [ "${FIX_FORMAT}" = "ON" ]; then
            print_error "❌ clang-format cannot auto-fix these files. Manual review needed."
        else
            print_info "💡 Run with --fix to auto-fix format issues."
        fi
        return 1
    else
        print_info "🎉 All files passed format check!"
        return 0
    fi
}

# Kotlin 代码风格检查（ktlint）
run_ktlint_check() {
    if ! command -v "${KTLINT}" &>/dev/null; then
        print_warn "ktlint not found, skipping Kotlin format check."
        print_info "  Install: brew install ktlint"
        return 0
    fi

    # 收集存在 .kt 文件的目录
    local kt_dirs=()
    for dir in "${SOURCE_DIRS[@]}"; do
        if [ -d "${dir}" ]; then
            local has_kt=false
            while IFS= read -r -d '' _; do
                has_kt=true
                break
            done < <(find "${dir}" -type f -name "*.kt" -print0 2>/dev/null)
            if [ "${has_kt}" = true ]; then
                kt_dirs+=("${dir}")
            fi
        fi
    done

    if [ ${#kt_dirs[@]} -eq 0 ]; then
        print_info "No Kotlin files found, skipping ktlint."
        return 0
    fi

    print_info "========================================"
    print_info "Starting Kotlin format check (ktlint)..."
    print_info "========================================"
    print_info "Using ktlint: $(${KTLINT} --version 2>/dev/null || echo 'unknown')"

    local ktlint_args=()
    if [ "${FIX_FORMAT}" = "ON" ]; then
        ktlint_args+=("-F")
    fi

    cd "${PROJECT_DIR}"
    local ktlint_exit=0
    "${KTLINT}" "${ktlint_args[@]}" "${kt_dirs[@]}" || ktlint_exit=$?

    if [ "${ktlint_exit}" -ne 0 ]; then
        echo ""
        if [ "${FIX_FORMAT}" != "ON" ]; then
            print_error "❌ Kotlin format issues found."
            print_info "  Run with --fix to auto-fix Kotlin format issues."
        else
            print_error "❌ ktlint exited with errors."
        fi
        return 1
    fi

    print_info "🎉 Kotlin format check passed!"
    return 0
}

# 主函数
main() {
    echo "=========================================="
    echo -e "${BLUE}Slark Code Format${NC}"
    echo "=========================================="
    echo "  Project Directory : ${PROJECT_DIR}"
    echo "  Fix Format        : ${FIX_FORMAT}"
    echo "  Verbose Output    : ${VERBOSE}"
    echo "  File Path Display : ${SHOW_FULL_PATH}"
    echo "=========================================="
    echo ""
    
    local exit_code=0
    
    if ! run_format_check; then
        exit_code=1
    fi
    
    echo ""

    if ! run_ktlint_check; then
        exit_code=1
    fi
    
    echo ""
    echo "=========================================="
    if [ "${exit_code}" -eq 0 ]; then
        print_info "🎉 Format check passed successfully!"
    else
        print_error "❌ Format check failed - please fix the issues above!"
    fi
    echo "=========================================="
    
    exit ${exit_code}
}

main
