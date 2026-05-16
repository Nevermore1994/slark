#!/bin/bash
# iOS 平台构建配置脚本

# iOS 配置常量（用变量而非 readonly，避免 source 时重复声明报错）
IOS_DEPLOYMENT_TARGET="16.5"
IOS_PLATFORM="OS64"
IOS_ENABLE_BITCODE="FALSE"

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
    
    # iOS 不支持运行可执行文件，强制禁用测试
    local cmake_options=$(build_cmake_options "$disable_http" true false)
    
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
        print_success "iOS 项目生成成功: ${output_dir}/slark.xcodeproj"
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

# 检查编译 framework 所需依赖
check_framework_dependencies() {
    if ! command -v xcodebuild &> /dev/null; then
        print_error "未找到 xcodebuild 命令，请在 macOS 上运行"
        exit 1
    fi
    print_success "iOS framework 编译依赖检查通过"
}

# 编译 framework（类似 Android 的 --aar）
build_framework() {
    local script_dir="$1"
    local build_type="$2"   # Debug | Release
    local output_dir="$3"   # 可选，framework 拷贝目标目录
    local clean_build="$4"  # true = 清理缓存后重新编译

    print_info "编译 iOS framework (${build_type})..."

    local proj_dir="${script_dir}/cmake-build-${build_type}/iOS"
    local framework_name="slark.framework"
    local framework_path="${proj_dir}/${build_type}-iphoneos/${framework_name}"

    if [[ "$clean_build" == true ]]; then
        print_info "清理 iOS 构建缓存: ${proj_dir}"
        rm -rf "${proj_dir}"
    fi

    # =========================================================================
    # 第一步：预先构建支持多架构的 OpenSSL
    # =========================================================================
    if [[ "$ENABLE_HTTPS" == "ON" ]]; then
        print_info "预先构建支持多架构的 OpenSSL..."
        build_openssl_multi_arch "$script_dir" "$build_type"
    fi

    # =========================================================================
    # 第二步：生成 Xcode 项目（使用已构建好的 OpenSSL）
    # =========================================================================
    # 每次都重新运行 cmake configure，cmake 本身是幂等的，只有变化时才会重新生成
    print_info "Xcode 工程不存在，先生成工程..."
    generate_ios_project "$script_dir" "$build_type" false true false

    # 获取可用的开发者证书/签名团队 ID
    local code_sign_identity=""
    local development_team=""
    
    # 尝试从环境变量读取
    if [[ -n "$IOS_CODE_SIGN_IDENTITY" ]]; then
        code_sign_identity="CODE_SIGN_IDENTITY='${IOS_CODE_SIGN_IDENTITY}'"
    fi
    
    if [[ -n "$IOS_DEVELOPMENT_TEAM" ]]; then
        development_team="DEVELOPMENT_TEAM='${IOS_DEVELOPMENT_TEAM}'"
    fi

    # 编译真机版本（只编译 slark target，跳过 ZERO_CHECK）
    print_info "编译真机版本 (iphoneos)..."
    local openssl_iphoneos="${proj_dir}/deps/openssl/iphoneos"
    xcodebuild build \
        -project "${proj_dir}/slark.xcodeproj" \
        -target slark \
        -configuration "${build_type}" \
        -sdk iphoneos \
        -arch arm64 \
        ${code_sign_identity} \
        ${development_team} \
        CODE_SIGNING_REQUIRED=NO \
        CODE_SIGNING_ALLOWED=NO \
        ONLY_ACTIVE_ARCH=NO \
        SUPPORTED_PLATFORMS=iphoneos \
        WARNING_CFLAGS="-Wno-old-style-cast" \
        HEADER_SEARCH_PATHS="\$(inherited) ${openssl_iphoneos}/include" \
        LIBRARY_SEARCH_PATHS="\$(inherited) ${openssl_iphoneos}/lib"

    # 编译模拟器版本（只编译 slark target，跳过 ZERO_CHECK）
    print_info "编译模拟器版本 (iphonesimulator)..."
    local openssl_simulator="${proj_dir}/deps/openssl/iphonesimulator"
    xcodebuild build \
        -project "${proj_dir}/slark.xcodeproj" \
        -target slark \
        -configuration "${build_type}" \
        -sdk iphonesimulator \
        -arch arm64 \
        -arch x86_64 \
        ${code_sign_identity} \
        ${development_team} \
        CODE_SIGNING_REQUIRED=NO \
        CODE_SIGNING_ALLOWED=NO \
        ONLY_ACTIVE_ARCH=NO \
        SUPPORTED_PLATFORMS=iphonesimulator \
        SUPPORTED_DEVICE_FAMILIES="1 2" \
        WARNING_CFLAGS="-Wno-old-style-cast" \
        HEADER_SEARCH_PATHS="\$(inherited) ${openssl_simulator}/include" \
        LIBRARY_SEARCH_PATHS="\$(inherited) ${openssl_simulator}/lib"

    # 创建 XCFramework
    local xcframework_output="${proj_dir}/${build_type}/${framework_name%.*}.xcframework"
    rm -rf "${xcframework_output}"

    local iphoneos_framework="${proj_dir}/${build_type}-iphoneos/${framework_name}"
    local simulator_framework="${proj_dir}/${build_type}-iphonesimulator/${framework_name}"

    if [[ ! -d "$iphoneos_framework" ]]; then
        print_error "真机 framework 不存在: $iphoneos_framework"
        exit 1
    fi

    if [[ ! -d "$simulator_framework" ]]; then
        print_error "模拟器 framework 不存在: $simulator_framework"
        exit 1
    fi

    print_info "创建 XCFramework..."
    xcodebuild -create-xcframework \
        -framework "$iphoneos_framework" \
        -framework "$simulator_framework" \
        -output "${xcframework_output}"

    print_success "XCFramework 生成成功: ${xcframework_output}"

    # 拷贝到指定目录
    if [[ -n "$output_dir" ]]; then
        mkdir -p "$output_dir"
        cp -R "${xcframework_output}" "$output_dir/"
        print_success "已拷贝到: ${output_dir}/$(basename "${xcframework_output}")"
    fi

    # 同时拷贝单独的 framework（方便只需要真机版本的场景）
    if [[ -n "$output_dir" ]]; then
        mkdir -p "${output_dir}/iphoneos"
        mkdir -p "${output_dir}/iphonesimulator"
        cp -R "$iphoneos_framework" "${output_dir}/iphoneos/"
        cp -R "$simulator_framework" "${output_dir}/iphonesimulator/"
        print_success "真机 framework 已拷贝到: ${output_dir}/iphoneos/"
        print_success "模拟器 framework 已拷贝到: ${output_dir}/iphonesimulator/"
    fi
}

# 构建支持多架构的 OpenSSL（真机 + 模拟器）
# 由于真机 arm64 和模拟器 arm64 架构名相同但二进制不兼容，
# 真机（arm64）和模拟器（x86_64 + arm64 fat）分别构建，
# 模拟器两个架构最终通过 lipo 合并为 fat library
build_openssl_multi_arch() {
    local script_dir="$1"
    local build_type="$2"
    
    local openssl_version="3.0.13"
    local openssl_src_dir="${script_dir}/.fetchcontent_cache/openssl/openssl-${openssl_version}"
    local openssl_base_dir="${script_dir}/cmake-build-${build_type}/iOS/deps/openssl"
    
    # 检查最终合并后的 fat library 是否已存在
    if [[ -f "${openssl_base_dir}/iphoneos/lib/libssl.a" && \
          -f "${openssl_base_dir}/iphonesimulator/lib/libssl.a" ]]; then
        print_success "OpenSSL 多平台库已存在（iphoneos + iphonesimulator fat）"
        return 0
    fi
    
    print_info "构建 OpenSSL 多平台版本..."
    
    # 确保源码已下载
    if [[ ! -d "$openssl_src_dir" ]]; then
        print_info "下载 OpenSSL 源码..."
        mkdir -p "$(dirname "$openssl_src_dir")"
        local openssl_url="https://github.com/openssl/openssl/releases/download/openssl-${openssl_version}/openssl-${openssl_version}.tar.gz"
        curl -L -o "${script_dir}/.fetchcontent_cache/openssl.tar.gz" "$openssl_url"
        tar -xzf "${script_dir}/.fetchcontent_cache/openssl.tar.gz" -C "$(dirname "$openssl_src_dir")"
        rm -f "${script_dir}/.fetchcontent_cache/openssl.tar.gz"
    fi
    
    if [[ ! -d "$openssl_src_dir" ]]; then
        print_error "OpenSSL 源码下载失败"
        exit 1
    fi
    
    local ios_deployment_target="16.5"
    local ncpu
    ncpu=$(sysctl -n hw.ncpu)

    # 构建单个架构的 OpenSSL，安装到临时目录
    # 参数：arch openssl_target sdk_type install_dir
    build_openssl_arch() {
        local arch="$1"
        local openssl_target="$2"
        local sdk_type="$3"
        local install_dir="$4"

        if [[ -f "${install_dir}/lib/libssl.a" ]]; then
            print_info "OpenSSL ${arch}/${sdk_type} 已存在，跳过"
            return 0
        fi

        print_info "构建 OpenSSL ${arch} (${sdk_type})..."

        local build_dir="${openssl_base_dir}/build-${sdk_type}-${arch}"
        rm -rf "$build_dir"
        mkdir -p "$build_dir"
        mkdir -p "$install_dir"

        rsync -a --exclude='*.o' --exclude='*.a' --exclude='*.so' \
            --exclude='*.dylib' --exclude='Makefile' --exclude='configdata.pm' \
            --exclude='*.obj' --exclude='*.lib' \
            "${openssl_src_dir}/" "$build_dir/"

        local sdk_path
        sdk_path=$(xcrun --show-sdk-path --sdk "$sdk_type")
        local target_flag="${arch}-apple-ios${ios_deployment_target}"
        [[ "$sdk_type" == "iphonesimulator" ]] && target_flag="${target_flag}-simulator"
        local cflags="-target ${target_flag} -isysroot ${sdk_path}"

        pushd "$build_dir" > /dev/null
        CC=clang CFLAGS="$cflags" \
        ./Configure "$openssl_target" \
            --prefix="$install_dir" \
            --openssldir="$install_dir/ssl" \
            --sysroot="$sdk_path" \
            no-shared no-tests no-legacy no-module || { print_error "OpenSSL configure failed: ${arch}/${sdk_type}"; exit 1; }

        make -j"$ncpu" build_libs || { print_error "OpenSSL build failed: ${arch}/${sdk_type}"; exit 1; }
        make install_dev          || { print_error "OpenSSL install failed: ${arch}/${sdk_type}"; exit 1; }

        # 用 libtool 重新打包为 Apple 格式静态库
        for lib in ssl crypto; do
            local lib_path="${install_dir}/lib/lib${lib}.a"
            if [[ -f "$lib_path" ]]; then
                local repack_dir="${build_dir}/repack_${lib}"
                rm -rf "$repack_dir" && mkdir -p "$repack_dir"
                (cd "$repack_dir" && ar -x "$lib_path" && libtool -static -o "$lib_path" ./*.o)
            fi
        done

        popd > /dev/null
    }

    # 构建真机（arm64 only）
    build_openssl_arch "arm64" "ios64-xcrun" "iphoneos" "${openssl_base_dir}/iphoneos"

    # 构建模拟器：x86_64 必须构建；Apple Silicon Mac 上额外构建 arm64 模拟器，最后 lipo 合并
    local sim_dir="${openssl_base_dir}/iphonesimulator"

    if [[ -f "${sim_dir}/lib/libssl.a" ]]; then
        print_info "OpenSSL iphonesimulator fat library 已存在，跳过"
    else
        build_openssl_arch "x86_64" "iossimulator-xcrun" "iphonesimulator" "${openssl_base_dir}/iphonesimulator-x86_64"

        if [[ "$(uname -m)" == "arm64" ]]; then
            build_openssl_arch "arm64" "iossimulator-xcrun" "iphonesimulator" "${openssl_base_dir}/iphonesimulator-arm64"

            # lipo 合并 x86_64 + arm64 为模拟器 fat library
            print_info "合并模拟器 fat library (x86_64 + arm64)..."
            mkdir -p "${sim_dir}/lib" "${sim_dir}/include"
            cp -R "${openssl_base_dir}/iphonesimulator-x86_64/include/" "${sim_dir}/include/"
            for lib in ssl crypto; do
                lipo -create \
                    "${openssl_base_dir}/iphonesimulator-x86_64/lib/lib${lib}.a" \
                    "${openssl_base_dir}/iphonesimulator-arm64/lib/lib${lib}.a" \
                    -output "${sim_dir}/lib/lib${lib}.a" \
                    || { print_error "lipo 合并 lib${lib}.a 失败"; exit 1; }
            done
        else
            # Intel Mac：直接将 x86_64 结果作为模拟器库
            cp -R "${openssl_base_dir}/iphonesimulator-x86_64/" "${sim_dir}/"
        fi
    fi

    print_success "OpenSSL 多平台库构建完成:"
    print_info "  真机:   $(lipo -info "${openssl_base_dir}/iphoneos/lib/libssl.a" 2>/dev/null)"
    print_info "  模拟器: $(lipo -info "${sim_dir}/lib/libssl.a" 2>/dev/null)"
}

# 如果直接执行此脚本
if [[ "${BASH_SOURCE[0]}" == "${0}" ]]; then
    SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
    source "${SCRIPT_DIR}/scripts/build_utils.sh"
    
    BUILD_TYPE="Debug"
    DISABLE_HTTP=false
    DISABLE_TEST=false
    DEMO=false
    BUILD_FRAMEWORK=false
    FRAMEWORK_OUTPUT_DIR=""
    CLEAN_BUILD=false
    
    while [[ $# -gt 0 ]]; do
        case "$1" in
            -t|--type) BUILD_TYPE="$2"; shift 2 ;;
            --disable_http) DISABLE_HTTP=true; ENABLE_HTTPS="OFF"; shift ;;
            --disable_test) DISABLE_TEST=true; shift ;;
            --demo) DEMO=true; shift ;;
            --framework) BUILD_FRAMEWORK=true; shift ;;
            --clean-build) CLEAN_BUILD=true; shift ;;
            --framework-output)
                [[ -z "$2" ]] && { print_error "选项 $1 需要参数"; exit 1; }
                FRAMEWORK_OUTPUT_DIR="$2"; shift 2 ;;
            -h|--help)
                echo "用法: $(basename "$0") [-t Debug|Release] [--framework] [--framework-output <dir>] [--demo]"
                exit 0 ;;
            *) shift ;;
        esac
    done
    
    if [[ "$BUILD_FRAMEWORK" == true ]]; then
        check_framework_dependencies
        export ENABLE_HTTPS
        build_framework "$SCRIPT_DIR" "$BUILD_TYPE" "$FRAMEWORK_OUTPUT_DIR" "$CLEAN_BUILD"
    else
        check_ios_dependencies
        clean_build_cache "$SCRIPT_DIR"
        generate_ios_project "$SCRIPT_DIR" "$BUILD_TYPE" "$DISABLE_HTTP" "$DISABLE_TEST" "$DEMO"
    fi
fi
