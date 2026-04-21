#!/bin/bash
# Android 平台构建配置脚本

set -e

# Android 配置常量
readonly ANDROID_ABI="arm64-v8a"
readonly ANDROID_PLATFORM_VERSION="29"

# 生成 Android 项目
generate_android_project() {
    local script_dir="$1"
    local build_type="$2"
    local disable_http="$3"
    local disable_test="$4"
    local demo="$5"
    
    print_info "生成 Android Ninja 项目..."
    
    local output_dir="${script_dir}/cmake-build-${build_type}/Android"
    local ndk_toolchain="${ANDROID_NDK_ROOT}/build/cmake/android.toolchain.cmake"
    
    if [[ ! -f "$ndk_toolchain" ]]; then
        print_error "未找到 Android NDK 工具链文件: $ndk_toolchain"
        exit 1
    fi
    
    local cmake_options=$(build_cmake_options "$disable_http" "$disable_test" false)
    
    cmake -B "$output_dir" -G Ninja \
        -DCMAKE_SYSTEM_NAME=Android \
        -DBUILD_PLATFORM=Android \
        -DCMAKE_TOOLCHAIN_FILE="$ndk_toolchain" \
        -DANDROID_ABI=${ANDROID_ABI} \
        -DCMAKE_BUILD_TYPE="$build_type" \
        -DANDROID_PLATFORM=${ANDROID_PLATFORM_VERSION} \
        $cmake_options
    
    print_success "Android 项目生成成功"
    
    if [[ "$demo" == true ]]; then
        local demo_dir="${script_dir}/demo/Android"
        [[ -d "$demo_dir" ]] && { print_info "打开 Android Demo 项目..."; open -a "Android Studio" "$demo_dir"; } \
            || print_warning "未找到 Android Demo 目录: $demo_dir"
    else
        local android_src="${script_dir}/src/platform/Android"
        [[ -d "$android_src" ]] && { print_info "打开 Android 源码项目..."; open -a "Android Studio" "$android_src"; }
    fi
}

# 检查 Android 依赖
check_android_dependencies() {
    if ! command -v ninja &> /dev/null; then
        print_error "未找到 ninja 命令，请先安装 Ninja"
        exit 1
    fi
    
    if [[ -z "$ANDROID_NDK_ROOT" ]]; then
        print_error "未设置 ANDROID_NDK_ROOT 环境变量"
        print_error "请设置 Android NDK 路径，例如："
        print_error "  export ANDROID_NDK_ROOT=/path/to/android-ndk"
        exit 1
    fi
    
    print_success "Android 依赖检查通过"
}

# 如果直接执行此脚本
if [[ "${BASH_SOURCE[0]}" == "${0}" ]]; then
    SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
    source "${SCRIPT_DIR}/scripts/build_utils.sh"
    
    BUILD_TYPE="Debug"
    DISABLE_HTTP=false
    DISABLE_TEST=false
    DEMO=false
    
    while [[ $# -gt 0 ]]; do
        case "$1" in
            -t|--type) BUILD_TYPE="$2"; shift 2 ;;
            --disable_http) DISABLE_HTTP=true; shift ;;
            --disable_test) DISABLE_TEST=true; shift ;;
            --demo) DEMO=true; shift ;;
            -h|--help) echo "用法: $(basename "$0") [-t Debug|Release] [--demo]"; exit 0 ;;
            *) shift ;;
        esac
    done
    
    check_android_dependencies
    clean_build_cache "$SCRIPT_DIR"
    generate_android_project "$SCRIPT_DIR" "$BUILD_TYPE" "$DISABLE_HTTP" "$DISABLE_TEST" "$DEMO"
fi
