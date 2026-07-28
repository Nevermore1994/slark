#!/bin/bash
# Android 平台构建配置脚本

# Android 配置常量（用变量而非 readonly，避免 source 时重复声明报错）
ANDROID_ABI="arm64-v8a"
ANDROID_PLATFORM_VERSION="29"

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
    local fc_cache="${script_dir}/.fetchcontent_cache/Android"
    
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
        -DFETCHCONTENT_BASE_DIR="$fc_cache" \
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

# 编译 AAR（含 C++ libslark.so）
# 使用 Gradle :sdk:assemble<Variant> 驱动，C++ 由 externalNativeBuild 自动编译
build_aar() {
    local script_dir="$1"
    local build_type="$2"   # Debug | Release
    local output_dir="$3"   # 可选，AAR 拷贝目标目录
    local clean_build="$4"  # true = 清理缓存后重新编译

    local android_dir="${script_dir}/src/platform/Android"
    local gradlew="${android_dir}/gradlew"

    if [[ ! -x "$gradlew" ]]; then
        print_error "未找到可执行的 gradlew: $gradlew"
        exit 1
    fi

    local build_type_lower
    build_type_lower=$(echo "$build_type" | tr '[:upper:]' '[:lower:]')
    local variant
    if [[ "$build_type_lower" == "release" ]]; then
        variant="Release"
    else
        variant="Debug"
    fi

    if [[ "$clean_build" == true ]]; then
        print_info "清理 Android 构建缓存..."
        "${gradlew}" -p "${android_dir}" ":sdk:clean" --no-daemon
    fi

    print_info "编译 AAR (variant=${variant})，C++ 将同步编译..."
    "${gradlew}" -p "${android_dir}" ":sdk:assemble${variant}" --no-daemon
    print_success "AAR 编译完成"

    local aar_dir="${script_dir}/build/android/sdk/outputs/aar"
    local aar_file
    local variant_lower
    variant_lower=$(echo "$variant" | tr '[:upper:]' '[:lower:]')
    aar_file=$(find "$aar_dir" -name "*-${variant_lower}.aar" 2>/dev/null | head -1)
    if [[ -z "$aar_file" ]]; then
        aar_file=$(find "$aar_dir" -name "*.aar" 2>/dev/null | head -1)
    fi

    if [[ -n "$aar_file" ]]; then
        print_success "AAR 产物: $aar_file"
        if [[ -n "$output_dir" ]]; then
            mkdir -p "$output_dir"
            cp "$aar_file" "$output_dir/"
            print_success "已拷贝到: ${output_dir}/$(basename "$aar_file")"
        fi
    else
        print_warning "未在 $aar_dir 找到 AAR 产物，请检查构建日志"
    fi
}

# 检查生成 CMake Ninja 工程所需依赖（需要 ninja + ANDROID_NDK_ROOT）
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

# 检查编译 AAR 所需依赖（只需要 gradlew，NDK 由 Gradle/local.properties 管理）
check_aar_dependencies() {
    local script_dir="$1"
    local android_dir="${script_dir}/src/platform/Android"
    local gradlew="${android_dir}/gradlew"

    if [[ ! -x "$gradlew" ]]; then
        print_error "未找到可执行的 gradlew: $gradlew"
        exit 1
    fi

    print_success "AAR 编译依赖检查通过"
}

# 如果直接执行此脚本
if [[ "${BASH_SOURCE[0]}" == "${0}" ]]; then
    SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
    source "${SCRIPT_DIR}/scripts/build_utils.sh"
    
    BUILD_TYPE="Debug"
    DISABLE_HTTP=false
    DISABLE_TEST=false
    DEMO=false
    BUILD_AAR=false
    AAR_OUTPUT_DIR=""
    CLEAN_BUILD=false
    
    while [[ $# -gt 0 ]]; do
        case "$1" in
            -t|--type) BUILD_TYPE="$2"; shift 2 ;;
            --disable_http) DISABLE_HTTP=true; shift ;;
            --disable_test) DISABLE_TEST=true; shift ;;
            --demo) DEMO=true; shift ;;
            --aar) BUILD_AAR=true; shift ;;
            --clean-build) CLEAN_BUILD=true; shift ;;
            --aar-output)
                [[ -z "$2" ]] && { print_error "选项 $1 需要参数"; exit 1; }
                AAR_OUTPUT_DIR="$2"; shift 2 ;;
            -h|--help)
                echo "用法: $(basename "$0") [-t Debug|Release] [--aar] [--aar-output <dir>] [--demo]"
                exit 0 ;;
            *) shift ;;
        esac
    done
    
    if [[ "$BUILD_AAR" == true ]]; then
        check_aar_dependencies "$SCRIPT_DIR"
        build_aar "$SCRIPT_DIR" "$BUILD_TYPE" "$AAR_OUTPUT_DIR" "$CLEAN_BUILD"
    else
        check_android_dependencies
        clean_build_cache "$SCRIPT_DIR" "Android"
        generate_android_project "$SCRIPT_DIR" "$BUILD_TYPE" "$DISABLE_HTTP" "$DISABLE_TEST" "$DEMO"
    fi
fi
