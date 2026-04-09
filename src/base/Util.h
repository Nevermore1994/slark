//
// Created by Nevermore
// slark Util
// Copyright (c) 2024 Nevermore All rights reserved.

#pragma once

#include "DataView.hpp"
#include "Error.h"
#include "Types.h"

#include <bit>
#include <cstdint>
#include <format>
#include <memory>
#include <string>

namespace slark::Util {

template <typename T>
Expected<uint64_t> getWeakPtrAddress(const std::weak_ptr<T>& ptr) noexcept {
    if (auto p = ptr.lock()) {
        return std::hash<std::shared_ptr<T>>()(p);
    }
    return std::unexpected(DataError::NullPointer);
}

std::string genRandomName(std::string_view prefix) noexcept;

template <typename T>
    requires std::is_unsigned_v<T>
std::string toStringLE(T value) noexcept {
    std::string ret(sizeof(T), '\0');
    auto bytes = std::bit_cast<std::array<char, sizeof(T)>>(value);
    if constexpr (std::endian::native == std::endian::little) {
        std::copy(bytes.begin(), bytes.end(), ret.begin());
    } else {
        std::copy(bytes.rbegin(), bytes.rend(), ret.begin());
    }
    return ret;
}

template <typename T>
    requires std::is_unsigned_v<T>
std::string toStringBE(T value) noexcept {
    std::string ret(sizeof(T), '\0');
    auto bytes = std::bit_cast<std::array<char, sizeof(T)>>(value);
    if constexpr (std::endian::native == std::endian::big) {
        std::copy(bytes.begin(), bytes.end(), ret.begin());
    } else {
        std::copy(bytes.rbegin(), bytes.rend(), ret.begin());
    }
    return ret;
}

struct Golomb {
    static bool readBit(DataView view, int32_t& offset) noexcept;

    static uint32_t readBits(DataView view, int32_t& offset, int32_t numBits) noexcept;

    static uint32_t readUe(DataView view, int32_t& offset) noexcept;
};

} // namespace slark::Util
