//
//  Util.cpp
//  slark
//
//  Created by Nevermore on 2023/10/20.
//  Copyright (c) 2023 Nevermore All rights reserved.

#include "Util.hpp"
#include "Random.hpp"

namespace slark::Util {

std::string genRandomName(const std::string& namePrefix) noexcept {
    return namePrefix + Random::randomString(8);
}

bool readByte(DataView view, uint8_t& value) noexcept {
    if (view.empty()) {
        return false;
    }
    auto func = [&](size_t pos) {
        return view[pos];
    };
    value = func(0);
    return true;
}

bool read2ByteBE(DataView view, uint16_t& value) noexcept {
    if (view.size() < 2) {
        return false;
    }
    auto func = [&](size_t pos) {
        return view[pos] << ((1 - pos) * 8);
    };
    value = static_cast<uint16_t>(func(0) | func(1));
    return true;
}

bool read3ByteBE(DataView view, uint32_t& value) noexcept {
    if (view.size() < 3) {
        return false;
    }
    auto func = [&](size_t pos) {
        return view[pos] << ((2 - pos) * 8);
    };
    value = static_cast<uint32_t>(func(0) | func(1) | func(2));
    return true;
}

bool read4ByteBE(DataView view, uint32_t& value) noexcept {
    if (view.size() < 4) {
        return false;
    }
    auto func = [&](size_t pos) {
        return view[pos] << ((3 - pos) * 8);
    };
    value = static_cast<uint32_t>(func(0) | func(1) | func(2) | func(3));
    return true;
}

bool read8ByteBE(DataView view, uint64_t& value) noexcept {
    if (view.size() < 8) {
        return false;
    }
    auto func = [&](size_t pos) {
        return view[pos] << ((7 - pos) * 8);
    };
    value = static_cast<uint64_t>(func(0) | func(1) | func(2) | func(3) | func(4) | func(5) | func(6) | func(7));
    return true;
}

bool read2ByteLE(DataView view, uint16_t& value) noexcept {
    if (view.size() < 2) {
        return false;
    }
    auto func = [&](size_t pos) {
        return view[pos] << (pos * 8);
    };
    value = static_cast<uint16_t>(func(1) | func(0));
    return true;
}

bool read3ByteLE(DataView view, uint32_t& value) noexcept {
    if (view.size() < 3) {
        return false;
    }
    auto func = [&](size_t pos) {
        return view[pos] << (pos * 8);
    };
    value = static_cast<uint32_t>(func(2) | func(1) | func(0));
    return true;
}

bool read4ByteLE(DataView view, uint32_t& value) noexcept {
    if (view.size() < 4) {
        return false;
    }
    auto func = [&](size_t pos) {
        return view[pos] << (pos * 8);
    };
    value = static_cast<uint32_t>(func(3) | func(2) | func(1) | func(0));
    return true;
}

bool read8ByteLE(DataView view, uint64_t& value) noexcept {
    if (view.size() < 8) {
        return false;
    }
    auto func = [&](size_t pos) {
        return view[pos] << (pos * 8);
    };
    value = static_cast<uint64_t>(func(7) | func(6) | func(5) | func(4) | func(3) | func(2) | func(1) | func(0));
    return true;
}

bool readBE(DataView view, uint32_t size, uint32_t& value) noexcept {
    if (view.size() < size) {
        return false;
    }
    auto mx = static_cast<size_t>(size - 1);
    auto func = [&](size_t pos) {
        return static_cast<uint32_t>(view[pos] << ((mx - pos) * 8));
    };
    for(size_t i = 0; i <= mx; i++) {
        value |= func(i);
    }
    return true;
}

bool readLE(DataView view, uint32_t size, uint32_t& value) noexcept {
    if (view.size() < size) {
        return false;
    }
    auto mx = static_cast<int32_t>(size - 1);
    auto func = [&](size_t pos) {
        return static_cast<uint32_t>(view[pos] << ((mx - pos) * 8));
    };
    for(int32_t i = mx; i >= 0; i--) {
        value |= func(i);
    }
    return true;
}

std::string uint32ToByteLE(uint32_t value) noexcept {
    std::string ret(4,'\0');
    ret[3] = static_cast<char>((value >> (8 * 3)) & 0xFF);
    ret[2] = static_cast<char>((value >> (8 * 2)) & 0xFF);
    ret[1] = static_cast<char>((value >> (8 * 1)) & 0xFF);
    ret[0] = static_cast<char>((value >> (8 * 0)) & 0xFF);
    return ret;
}

std::string uint32ToByteBE(uint32_t value) noexcept {
    std::string ret(4,'\0');
    ret[0] = static_cast<char>((value >> (8 * 3)) & 0xFF);
    ret[1] = static_cast<char>((value >> (8 * 2)) & 0xFF);
    ret[2] = static_cast<char>((value >> (8 * 1)) & 0xFF);
    ret[3] = static_cast<char>((value >> (8 * 0)) & 0xFF);
    return ret;
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

}

