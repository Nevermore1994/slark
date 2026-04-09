#!/bin/bash

# Slark 仓库初始化脚本
# 用于首次克隆仓库后的初始化工作
# 使用方法: ./script/init-repo.sh

set -e

echo "🚀 Slark 仓库初始化开始..."
echo "================================"

# 检查是否在 Git 仓库中
if [ ! -d ".git" ]; then
    echo "❌ 错误: 当前目录不是 Git 仓库"
    exit 1
fi

# 检查 Git LFS 是否已安装
if ! command -v git-lfs &> /dev/null; then
    echo "❌ 错误: Git LFS 未安装"
    echo "请先安装 Git LFS:"
    echo "  - macOS: brew install git-lfs"
    echo "  - Ubuntu/Debian: sudo apt-get install git-lfs"
    echo "  - CentOS/RHEL: sudo yum install git-lfs"
    echo "安装后运行: git lfs install"
    exit 1
fi

echo "✅ Git LFS 已安装"

# 步骤1: 初始化 Git LFS
echo ""
echo "📦 步骤1: 初始化 Git LFS..."
git lfs install

echo "✅ Git LFS 初始化完成"

# 步骤2: 拉取子模块
echo ""
echo "📦 步骤2: 初始化子模块..."
if [ -f ".gitmodules" ]; then
    echo "🔍 检测到子模块配置:"
    cat .gitmodules
    echo ""
    
    git submodule update --init --recursive
    echo "✅ 子模块初始化完成"
else
    echo "ℹ️  未检测到子模块配置，跳过此步骤"
fi

# 步骤3: 拉取 Git LFS 文件
echo ""
echo "📦 步骤3: 拉取 Git LFS 文件..."
if [ -f ".gitattributes" ]; then
    echo "🔍 Git LFS 跟踪的文件类型:"
    grep "filter=lfs" .gitattributes || echo "未找到 LFS 跟踪配置"
    echo ""
    
    git lfs pull
    echo "✅ Git LFS 文件拉取完成"
else
    echo "ℹ️  未检测到 Git LFS 配置，跳过此步骤"
fi

# 步骤4: 安装 Git hooks
echo ""
echo "📦 步骤4: 安装 Git hooks..."
if [ -f "script/install-hooks.sh" ]; then
    # 保存当前目录
    CURRENT_DIR="$(pwd)"
    
    # 切换到脚本目录执行
    cd script
    ./install-hooks.sh
    
    # 返回原目录
    cd "$CURRENT_DIR"
    
    echo "✅ Git hooks 安装完成"
else
    echo "ℹ️  未找到 hooks 安装脚本，跳过此步骤"
fi

# 步骤5: 验证初始化结果
echo ""
echo "📦 步骤5: 验证初始化结果..."

# 检查子模块状态
if [ -f ".gitmodules" ]; then
    echo "🔍 子模块状态:"
    git submodule status
    echo ""
fi

# 检查 LFS 文件状态
if [ -f ".gitattributes" ]; then
    echo "🔍 LFS 文件状态:"
    git lfs ls-files
    echo ""
fi

# 检查 hooks 状态
if [ -f ".git/hooks/pre-commit" ]; then
    echo "🔍 Git hooks 状态:"
    ls -la .git/hooks/ | grep -E "(pre-commit|post-commit)" || echo "未找到 hooks 文件"
    echo ""
fi

echo "🎉 Slark 仓库初始化完成!"
echo "================================"
echo ""
echo "📋 初始化结果汇总:"
echo "  ✅ Git LFS 已初始化"
if [ -f ".gitmodules" ]; then
    echo "  ✅ 子模块已拉取"
fi
if [ -f ".gitattributes" ]; then
    echo "  ✅ Git LFS 文件已拉取"
fi
if [ -f "script/install-hooks.sh" ]; then
    echo "  ✅ Git hooks 已安装"
fi
echo ""
echo "💡 后续操作提示:"
echo "  - 构建项目: ./build.sh"
echo "  - 运行测试: cd cmake-build-Debug && ctest"
echo ""
echo "🔧 开发工具:"
echo "  - 代码格式化: 提交时自动触发"
echo "  - 跳过格式化: git commit --no-verify"
echo ""