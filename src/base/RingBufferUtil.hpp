//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#pragma once

#include <algorithm>
#include <cstdint>

namespace slark::detail {

// 环形写入：src → dstArray[pos] 开始，到达末尾后绕回头部
template <typename T>
void wrapCopy(const T* src, T* dstArray, size_t capacity, size_t pos, uint32_t count) noexcept {
    auto first = std::min(static_cast<size_t>(count), capacity - pos);
    std::copy_n(src, first, dstArray + pos);
    if (first < count) {
        std::copy_n(src + first, count - first, dstArray);
    }
}

// 环形读取：srcArray[pos] 开始 → dst，到达末尾后绕回头部
template <typename T>
void wrapCopyFrom(const T* srcArray, size_t capacity, size_t pos, T* dst, uint32_t count) noexcept {
    auto first = std::min(static_cast<size_t>(count), capacity - pos);
    std::copy_n(srcArray + pos, first, dst);
    if (first < count) {
        std::copy_n(srcArray, count - first, dst + first);
    }
}

} // namespace slark::detail
