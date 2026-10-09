//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#include "Util.h"

#include "Random.hpp"

namespace slark::Util {

std::string genRandomName(std::string_view prefix) noexcept {
    return std::string(prefix) + Random::randomString(8);
}

bool Golomb::readBit(DataView view, int32_t& offset) noexcept {
    // Out-of-range reads yield 0 but still advance, so callers' loops always terminate.
    const auto current = offset++;
    if (current < 0) {
        return false;
    }
    const auto byteIndex = static_cast<size_t>(current / 8);
    if (byteIndex >= view.length()) {
        return false;
    }
    return (view[byteIndex] >> (7 - (current % 8))) & 0x01;
}

uint32_t Golomb::readBits(DataView view, int32_t& offset, int32_t numBits) noexcept {
    uint32_t result = 0;
    for (int32_t i = 0; i < numBits; ++i) {
        result = (result << 1) | static_cast<uint32_t>(Golomb::readBit(view, offset));
    }
    return result;
}

uint32_t Golomb::readUe(DataView view, int32_t& offset) noexcept {
    constexpr int32_t kMaxLeadingZeroBits = 31;
    int32_t leadingZeroBits = 0;
    while (!readBit(view, offset)) {
        if (++leadingZeroBits > kMaxLeadingZeroBits) {
            return 0;
        }
    }
    uint32_t value = (uint32_t{1} << leadingZeroBits) - 1;
    value += readBits(view, offset, leadingZeroBits);
    return value;
}

}  // namespace slark::Util
