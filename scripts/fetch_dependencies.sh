#!/bin/bash
#
# fetch_dependencies.sh - 预先下载所有依赖
#
# 用法: ./script/fetch_dependencies.sh
#

set -e

SLARK_ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
FETCHCONTENT_CACHE="${SLARK_ROOT_DIR}/.fetchcontent_cache"
TEMP_BUILD_DIR="${SLARK_ROOT_DIR}/build/_fetch_deps_temp"
THIRD_PARTY_SUBMODULE="${SLARK_ROOT_DIR}/thirdParty/slark_third_party"

echo "=========================================="
echo "Slark Fetch Dependencies"
echo "=========================================="
echo "  Root Dir  : ${SLARK_ROOT_DIR}"
echo "  Cache Dir : ${FETCHCONTENT_CACHE}"
echo "==========================================""

# =============================================================================
# Step 1: Git Submodule 初始化
# =============================================================================
init_submodules() {
    echo ""
    echo "[Step 1/3] Checking Git Submodules..."

    if [ ! -d "${THIRD_PARTY_SUBMODULE}/.git" ] && [ ! -f "${THIRD_PARTY_SUBMODULE}/.git" ]; then
        echo "  Initializing and updating submodules..."
        git -C "${SLARK_ROOT_DIR}" submodule update --init --recursive
    else
        echo "  Submodules already initialized."
    fi
}

# =============================================================================
# Step 2: Git LFS 拉取
# =============================================================================
pull_lfs_files() {
    echo ""
    echo "[Step 2/3] Checking Git LFS files..."

    # 检查 git-lfs 是否安装
    if ! command -v git-lfs &>/dev/null; then
        echo "  [WARN] git-lfs is not installed. Skipping LFS pull."
        echo "         If you need model/video files, install git-lfs first:"
        echo "           Ubuntu/Debian: sudo apt-get install -y git-lfs"
        echo "           CentOS/RHEL  : sudo yum install -y git-lfs"
        echo "           macOS (brew) : brew install git-lfs"
        return 0
    fi

    # 检查主仓库的 LFS 文件
    local need_lfs_pull=0
    local sample_file
    sample_file=$(find "${SLARK_ROOT_DIR}/config" -maxdepth 3 \( -name "*.tnnmodel" -o -name "*.onnx" -o -name "*.bin" \) -print -quit 2>/dev/null)

    if [ -n "${sample_file}" ]; then
        if head -c 48 "${sample_file}" 2>/dev/null | grep -q "version https://git-lfs"; then
            need_lfs_pull=1
        fi
    fi

    if [ "${need_lfs_pull}" -eq 1 ]; then
        echo "  Pulling LFS files from main repository..."
        git -C "${SLARK_ROOT_DIR}" lfs install --local
        git -C "${SLARK_ROOT_DIR}" lfs pull
        echo "  Main repository LFS files pulled."
    else
        echo "  Main repository LFS files already present."
    fi

    # 检查 submodule 的 LFS 文件
    if [ -d "${THIRD_PARTY_SUBMODULE}/.git" ] || [ -f "${THIRD_PARTY_SUBMODULE}/.git" ]; then
        local submodule_lfs_attrs="${THIRD_PARTY_SUBMODULE}/.gitattributes"
        if [ -f "${submodule_lfs_attrs}" ] && grep -q "filter=lfs" "${submodule_lfs_attrs}" 2>/dev/null; then
            local lib_file
            lib_file=$(find "${THIRD_PARTY_SUBMODULE}" -maxdepth 5 \( -name "*.a" -o -name "*.so" -o -name "*.dylib" \) -print -quit 2>/dev/null)

            if [ -n "${lib_file}" ]; then
                if head -c 48 "${lib_file}" 2>/dev/null | grep -q "version https://git-lfs"; then
                    echo "  Pulling LFS files from submodule..."
                    git -C "${THIRD_PARTY_SUBMODULE}" lfs install --local
                    git -C "${THIRD_PARTY_SUBMODULE}" lfs fetch --all
                    git -C "${THIRD_PARTY_SUBMODULE}" lfs checkout
                    echo "  Submodule LFS files pulled."
                else
                    echo "  Submodule LFS files already present."
                fi
            fi
        fi
    fi
}

# =============================================================================
# Step 3: FetchContent 依赖下载
# =============================================================================
fetch_cmake_deps() {
    echo ""
    echo "[Step 3/3] Checking FetchContent dependencies..."

    # 检查缓存目录是否已存在且完整
    if [ -d "${FETCHCONTENT_CACHE}" ]; then
        local deps_complete=true
        for dep in libfmt-src nlohmann_json-src plog-src gtest-src stb-src; do
            if [ ! -d "${FETCHCONTENT_CACHE}/${dep}" ]; then
                deps_complete=false
                break
            fi
        done

        if [ "${deps_complete}" = true ]; then
            echo "  All FetchContent dependencies already cached."
            return 0
        fi
    fi

    echo "  Fetching CMake dependencies..."

    # 创建临时构建目录
    mkdir -p "${TEMP_BUILD_DIR}"

    # 运行 CMake configure 来触发 FetchContent 下载
    cmake -S "${SLARK_ROOT_DIR}" \
        -B "${TEMP_BUILD_DIR}" \
        -DCMAKE_BUILD_TYPE=Release \
        -DDISABLE_TEST=OFF \
        -DDISABLE_HTTP=OFF \
        --no-warn-unused-cli \
        || {
            echo "  [WARN] CMake configure failed, but dependencies may still be cached."
            echo "         If build fails later, please check the error messages above."
        }

    # 清理临时构建目录
    rm -rf "${TEMP_BUILD_DIR}"

    echo "  FetchContent dependencies cached."
}

# =============================================================================
# Main
# =============================================================================
init_submodules
pull_lfs_files
fetch_cmake_deps

echo ""
echo "=========================================="
echo "[INFO] All dependencies fetched successfully!"
echo "=========================================="
echo ""
