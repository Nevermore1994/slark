#!/bin/bash

# Slark 构建脚本入口
# 用于生成 iOS/Android 平台项目或本地 Makefile 项目

set -e

readonly SCRIPT_VERSION="2.1.0"
readonly SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# 加载公共函数
source "${SCRIPT_DIR}/scripts/common.sh"

# 显示帮助信息
show_help() {
    cat << EOF
Slark 构建脚本 - 生成 iOS/Android 平台项目或本地 Makefile 项目

用法: $(basename "$0") [选项]

选项:
  -p, --platform <platform>  目标平台 (iOS 或 Android)
                              若不指定，则生成本地项目并编译运行测试
  -t, --type <type>          构建类型 (Debug 或 Release，默认: Debug)
  --disable_http             禁用 HTTP 功能
  --disable_test             禁用测试（本地构建时不运行测试）
  --with_core                本地构建时包含 core 模块（默认仅 base + http）
  --demo                     构建 Demo 项目
  --clean                    仅清理构建缓存，不生成项目
  -h, --help                 显示此帮助信息

示例:
  # 本地构建 base + http 并运行测试（默认行为）
  $(basename "$0")

  # 本地构建但不运行测试
  $(basename "$0") --disable_test

  # 本地构建包含 core 模块的完整项目
  $(basename "$0") --with_core

  # 生成 iOS Xcode 项目
  $(basename "$0") -p iOS -t Release

  # 生成 Android Ninja 项目
  $(basename "$0") -p Android

  # 仅清理构建缓存
  $(basename "$0") --clean

EOF
}

# 解析命令行参数
BUILD_TYPE="Debug"
PLATFORM=""
DISABLE_HTTP=false
DISABLE_TEST=false
WITH_CORE=false
DEMO=false
CLEAN_ONLY=false

while [[ $# -gt 0 ]]; do
    case "$1" in
        -p|--platform)
            [[ -z "$2" ]] && { print_error "选项 $1 需要参数"; show_help; exit 1; }
            PLATFORM="$2"
            shift 2
            ;;
        -t|--type)
            [[ -z "$2" ]] && { print_error "选项 $1 需要参数"; show_help; exit 1; }
            BUILD_TYPE="$2"
            shift 2
            ;;
        --disable_http) DISABLE_HTTP=true; shift ;;
        --disable_test) DISABLE_TEST=true; shift ;;
        --with_core) WITH_CORE=true; shift ;;
        --demo) DEMO=true; shift ;;
        --clean) CLEAN_ONLY=true; shift ;;
        -h|--help) show_help; exit 0 ;;
        *) print_error "未知选项: $1"; show_help; exit 1 ;;
    esac
done

# 主流程
print_info "Slark 构建脚本 v${SCRIPT_VERSION}"
print_info "项目根目录: $SCRIPT_DIR"

# 清理缓存
clean_build_cache "$SCRIPT_DIR"

[[ "$CLEAN_ONLY" == true ]] && { print_success "清理完成"; exit 0; }

# 检查 cmake
check_cmake_dependency

# 根据平台生成项目
case "$PLATFORM" in
    iOS)
        source "${SCRIPT_DIR}/scripts/ios.sh"
        check_ios_dependencies
        generate_ios_project "$SCRIPT_DIR" "$BUILD_TYPE" "$DISABLE_HTTP" "$DISABLE_TEST" "$DEMO"
        ;;
    Android)
        source "${SCRIPT_DIR}/scripts/android.sh"
        check_android_dependencies
        generate_android_project "$SCRIPT_DIR" "$BUILD_TYPE" "$DISABLE_HTTP" "$DISABLE_TEST" "$DEMO"
        ;;
    *)
        generate_local_project "$SCRIPT_DIR" "$BUILD_TYPE" "$DISABLE_HTTP" "$DISABLE_TEST" "$WITH_CORE"
        ;;
esac

print_success "构建脚本执行完成"
