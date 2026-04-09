//
//  ByteUtils.hpp
//  slark
//
//  Created by Nevermore on 2025/4/3.

#pragma once

#include "Types.h"

#include <string_view>

namespace slark {

inline constexpr ConstByteSpan makeSafeView(const Byte* data, std::size_t size) noexcept {
    if (data == nullptr || size == 0) {
        return {};
    }
    return ConstByteSpan(data, size);
}

inline constexpr ConstByteSpan toByteSpan(std::string_view str) noexcept {
    return ConstByteSpan(reinterpret_cast<const Byte*>(str.data()), str.size());
}

inline constexpr std::string_view toStringView(ConstByteSpan span) noexcept {
    return std::string_view(reinterpret_cast<const char*>(span.data()), span.size());
}

} // namespace slark
