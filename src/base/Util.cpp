//
// Created by Nevermore
// slark Util
// Copyright (c) 2024 Nevermore All rights reserved.

#include "Util.h"

#include "Random.hpp"

namespace slark::Util {

std::string genRandomName(std::string_view prefix) noexcept {
    return std::string(prefix) + Random::randomString(8);
}

bool Golomb::readBit(DataView view, int32_t& offset) noexcept {
    auto byteIndex = static_cast<uint32_t>(offset / 8);
    int32_t bitIndex = 7 - (offset % 8);
    ++offset;
    return (view[byteIndex] >> bitIndex) & 0x01;
}

uint32_t Golomb::readBits(DataView view, int32_t& offset, int32_t numBits) noexcept {
    uint32_t result = 0;
    for (int32_t i = 0; i < numBits; ++i) {
        result = (result << 1) | Golomb::readBit(view, offset);
    }
    return result;
}

uint32_t Golomb::readUe(DataView view, int32_t& offset) noexcept {
    int32_t leadingZeroBits = 0;
    while (!readBit(view, offset)) {
        ++leadingZeroBits;
    }
    uint32_t value = (1 << leadingZeroBits) - 1;
    value += readBits(view, offset, leadingZeroBits);
    return value;
}

} // namespace slark::Util
