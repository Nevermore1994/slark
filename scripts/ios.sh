#!/bin/bash
# iOS 平台构建配置脚本

set -e

# iOS 配置常量
readonly IOS_DEPLOYMENT_TARGET="16.5"
readonly IOS_PLATFORM="OS64"
readonly IOS_ENABLE_BITCODE="FALSE"

# 生成 iOS 项目
generate_ios_project() {
    local script_dir="$1"
    local build_type="$2"
    local disable_http="$3"
    local disable_test="$4"
    local demo="$5"
    
    print_info "生成 iOS Xcode 项目..."
    
    local output_dir="${script_dir}/cmake-build-${build_type}/iOS"
    local toolchain_file="${script_dir}/ios.toolchain.cmake"
    
    if [[ ! -f "$toolchain_file" ]]; then
        print_error "未找到 iOS 工具链文件: $toolchain_file"
        exit 1
    fi
    
    local cmake_options=$(build_cmake_options "$disable_http" "$disable_test" false)
    
    cmake -B "$output_dir" -G Xcode \
        -DCMAKE_TOOLCHAIN_FILE="$toolchain_file" \
        -DBUILD_PLATFORM=iOS \
        -DPLATFORM=${IOS_PLATFORM} \
        -DENABLE_BITCODE=${IOS_ENABLE_BITCODE} \
        -DDEPLOYMENT_TARGET=${IOS_DEPLOYMENT_TARGET} \
        -DCMAKE_BUILD_TYPE="$build_type" \
        $cmake_options
    
    print_success "iOS 项目生成成功"
    
    if [[ "$demo" == true ]]; then
        handle_ios_demo "$script_dir"
    else
        local xcode_project="${output_dir}/slark.xcodeproj"
        if [[ -d "$xcode_project" ]]; then
            print_info "打开 Xcode 项目..."
            open "$xcode_project"
        fi
    fi
}

# 处理 iOS Demo
handle_ios_demo() {
    local script_dir="$1"
    print_info "安装 iOS Demo 依赖..."
    
    local demo_dir="${script_dir}/demo/iOS/demo"
    [[ ! -d "$demo_dir" ]] && { print_warning "未找到 iOS Demo 目录: $demo_dir"; return; }
    
    cd "$demo_dir"
    if command -v pod &> /dev/null; then
        pod install --repo-update && print_success "CocoaPods 依赖安装成功" && open demo.xcworkspace \
            || print_warning "CocoaPods 安装失败，请手动执行"
    else
        print_warning "未找到 pod 命令，请手动安装 CocoaPods 依赖"
    fi
}

# 检查 iOS 依赖
check_ios_dependencies() {
    if ! command -v xcodebuild &> /dev/null; then
        print_error "未找到 xcodebuild 命令，请在 macOS 上运行"
        exit 1
    fi
    print_success "iOS 依赖检查通过"
}

# 如果直接执行此脚本
if [[ "${BASH_SOURCE[0]}" == "${0}" ]]; then
    SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
    source "${SCRIPT_DIR}/scripts/common.sh"
    
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
    
    check_ios_dependencies
    clean_build_cache "$SCRIPT_DIR"
    generate_ios_project "$SCRIPT_DIR" "$BUILD_TYPE" "$DISABLE_HTTP" "$DISABLE_TEST" "$DEMO"
fi
