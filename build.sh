#!/bin/bash

# Slark 构建脚本入口
# 用于生成 iOS/Android 平台项目或本地 Makefile 项目

set -e

readonly SCRIPT_VERSION="2.1.0"
readonly SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# 加载公共函数
source "${SCRIPT_DIR}/scripts/build_utils.sh"

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
  --aar                      编译 Android AAR（含 C++ libslark.so），需配合 -p Android
  --aar-output <dir>         AAR 产物拷贝目标目录（可选，配合 --aar 使用）
  --framework                编译 iOS framework（含真机+模拟器 XCFramework），需配合 -p iOS
  --framework-output <dir>   framework 产物拷贝目标目录（可选，配合 --framework 使用）
  --clean-build              编译前清理缓存（强制全量编译，适用于 --framework / --aar）
  --open                     生成项目后打开 IDE（Android Studio 或 Xcode）
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

  # 生成 Android Ninja 项目（仅生成 CMake 工程，不打 AAR）
  $(basename "$0") -p Android

  # 编译 Android AAR Release（含 C++ libslark.so）
  $(basename "$0") -p Android -t Release --aar

  # 编译 AAR 并拷贝到指定目录
  $(basename "$0") -p Android -t Release --aar --aar-output ./dist

  # 编译 iOS framework（含真机+模拟器 XCFramework）
  $(basename "$0") -p iOS -t Release --framework

  # 清理缓存后全量编译 iOS framework
  $(basename "$0") -p iOS -t Release --framework --clean-build

  # 编译 framework 并拷贝到指定目录
  $(basename "$0") -p iOS -t Release --framework --framework-output ./dist

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
BUILD_AAR=false
AAR_OUTPUT_DIR=""
BUILD_FRAMEWORK=false
FRAMEWORK_OUTPUT_DIR=""
OPEN_IDE=false
CLEAN_BUILD=false

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
        --aar) BUILD_AAR=true; shift ;;
        --aar-output)
            [[ -z "$2" ]] && { print_error "选项 $1 需要参数"; show_help; exit 1; }
            AAR_OUTPUT_DIR="$2"; shift 2 ;;
        --framework) BUILD_FRAMEWORK=true; shift ;;
        --clean-build) CLEAN_BUILD=true; shift ;;
        --framework-output)
            [[ -z "$2" ]] && { print_error "选项 $1 需要参数"; show_help; exit 1; }
            FRAMEWORK_OUTPUT_DIR="$2"; shift 2 ;;
        --open) OPEN_IDE=true; shift ;;
        --clean) CLEAN_ONLY=true; shift ;;
        -h|--help) show_help; exit 0 ;;
        *) print_error "未知选项: $1"; show_help; exit 1 ;;
    esac
done

# 主流程
print_info "Slark 构建脚本 v${SCRIPT_VERSION}"
print_info "项目根目录: $SCRIPT_DIR"

[[ "$CLEAN_ONLY" == true ]] && { clean_build_cache "$SCRIPT_DIR"; print_success "清理完成"; exit 0; }

# 根据平台生成项目
case "$PLATFORM" in
    iOS)
        source "${SCRIPT_DIR}/scripts/build_ios.sh"
        if [[ "$BUILD_FRAMEWORK" == true ]]; then
            check_framework_dependencies "$SCRIPT_DIR"
            build_framework "$SCRIPT_DIR" "$BUILD_TYPE" "$FRAMEWORK_OUTPUT_DIR" "$CLEAN_BUILD"
        else
            clean_build_cache "$SCRIPT_DIR"
            check_cmake_dependency
            check_ios_dependencies
            generate_ios_project "$SCRIPT_DIR" "$BUILD_TYPE" "$DISABLE_HTTP" "$DISABLE_TEST" "$DEMO"
        fi
        
        # 打开 Xcode
        if [[ "$OPEN_IDE" == true ]]; then
            xcode_project="${SCRIPT_DIR}/cmake-build-${BUILD_TYPE}/iOS/slark.xcodeproj"
            if [[ -d "$xcode_project" ]]; then
                print_info "打开 Xcode..."
                open "$xcode_project"
            else
                print_warning "未找到 Xcode 项目: $xcode_project"
            fi
        fi
        ;;
    Android)
        source "${SCRIPT_DIR}/scripts/build_android.sh"
        if [[ "$BUILD_AAR" == true ]]; then
            check_aar_dependencies "$SCRIPT_DIR"
            build_aar "$SCRIPT_DIR" "$BUILD_TYPE" "$AAR_OUTPUT_DIR" "$CLEAN_BUILD"
        else
            clean_build_cache "$SCRIPT_DIR"
            check_cmake_dependency
            check_android_dependencies
            generate_android_project "$SCRIPT_DIR" "$BUILD_TYPE" "$DISABLE_HTTP" "$DISABLE_TEST" "$DEMO"
        fi
        
        # 打开 Android Studio
        if [[ "$OPEN_IDE" == true ]]; then
            android_dir="${SCRIPT_DIR}/src/platform/Android"
            if [[ -d "$android_dir" ]]; then
                print_info "打开 Android Studio..."
                open -a "Android Studio" "$android_dir"
            else
                print_warning "未找到 Android 项目: $android_dir"
            fi
        fi
        ;;
    *)
        clean_build_cache "$SCRIPT_DIR"
        check_cmake_dependency
        generate_local_project "$SCRIPT_DIR" "$BUILD_TYPE" "$DISABLE_HTTP" "$DISABLE_TEST" "$WITH_CORE"
        ;;
esac

print_success "构建脚本执行完成"
