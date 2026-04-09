#!/bin/bash
# 构建脚本公共函数库

set -e

# 颜色定义
readonly RED='\033[0;31m'
readonly GREEN='\033[0;32m'
readonly YELLOW='\033[1;33m'
readonly BLUE='\033[0;34m'
readonly NC='\033[0m'

# 打印函数
print_info() {
    echo -e "${BLUE}[INFO]${NC} $1"
}

print_success() {
    echo -e "${GREEN}[SUCCESS]${NC} $1"
}

print_warning() {
    echo -e "${YELLOW}[WARNING]${NC} $1"
}

print_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

# 清理构建缓存
clean_build_cache() {
    local script_dir="$1"
    print_info "开始清理构建缓存..."
    
    local cache_dirs=(
        "${script_dir}/cmake-build-debug"
        "${script_dir}/cmake-build-release"
        "${script_dir}/cmake-build-Debug"
        "${script_dir}/cmake-build-Release"
    )
    
    for dir in "${cache_dirs[@]}"; do
        if [[ -d "$dir" ]]; then
            print_info "删除目录: $dir"
            rm -rf "$dir"
        fi
    done
    
    # 清理 Make 缓存
    if [[ -f "${script_dir}/Makefile" ]]; then
        cd "$script_dir"
        make clean 2>/dev/null || true
    fi
    
    print_success "构建缓存清理完成"
}

# 构建通用 cmake 选项
build_cmake_options() {
    local disable_http="$1"
    local disable_test="$2"
    local disable_core="$3"
    local options=()
    
    [[ "$disable_http" == true ]] && options+=("-DDISABLE_HTTP=ON")
    [[ "$disable_test" == true ]] && options+=("-DDISABLE_TEST=ON")
    [[ "$disable_core" == true ]] && options+=("-DDISABLE_CORE=ON")
    
    echo "${options[@]}"
}

# 检查 cmake 依赖
check_cmake_dependency() {
    if ! command -v cmake &> /dev/null; then
        print_error "未找到 cmake 命令，请先安装 CMake"
        exit 1
    fi
}

# 生成本地 Makefile 项目并编译运行测试
generate_local_project() {
    local script_dir="$1"
    local build_type="$2"
    local disable_http="$3"
    local disable_test="$4"
    local with_core="$5"
    
    # 根据 with_core 决定是否禁用 core 模块
    local disable_core="true"
    if [[ "$with_core" == true ]]; then
        disable_core="false"
        print_info "生成本地 Makefile 项目 (完整构建: base + http + core)..."
    else
        print_info "生成本地 Makefile 项目 (仅 base + http 模块)..."
    fi
    
    local output_dir="${script_dir}/cmake-build-${build_type}"
    local cmake_options=$(build_cmake_options "$disable_http" "$disable_test" "$disable_core")
    
    cmake -S "$script_dir" -B "$output_dir" -G "Unix Makefiles" \
        -DCMAKE_BUILD_TYPE="$build_type" \
        $cmake_options
    
    print_success "项目配置成功"
    
    # 编译项目
    local jobs=$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)
    print_info "开始编译 (并发数: $jobs)..."
    cmake --build "$output_dir" -- -j$jobs
    print_success "编译完成"
    
    # 运行测试
    if [[ "$disable_test" != true ]]; then
        run_tests "$output_dir"
    fi
    
    print_info "构建目录: $output_dir"
}

# 运行测试
run_tests() {
    local build_dir="$1"
    local ctest_output="${build_dir}/ctest-output.log"
    
    print_info "运行测试..."
    
    cd "$build_dir"
    
    # 检查是否有测试
    if ! ctest -N 2>/dev/null | grep -q "Total Tests"; then
        print_warning "未发现测试用例"
        return 0
    fi
    
    # 运行测试并捕获输出
    if ctest --output-on-failure -j$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4) 2>&1 | tee "$ctest_output"; then
        local passed=$(grep -E "^[0-9]+% tests passed" "$ctest_output" 2>/dev/null || echo "测试完成")
        print_success "测试通过: $passed"
    else
        print_error "测试失败，请查看日志: $ctest_output"
        return 1
    fi
}
