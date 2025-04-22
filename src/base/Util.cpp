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

template<typename T>
bool readLE(DataView view, uint32_t size, T& value) noexcept {
    static_assert(std::is_unsigned<T>::value, "T must be unsigned");
    if (size == 0 || size > sizeof(T) || view.size() < size) {
        return false;
    }
    value = 0;
    for (uint32_t i = 0; i < size; ++i) {
        value |= static_cast<T>(view[i]) << (8 * i);
    }
    return true;
}

template<typename T>
bool readBE(DataView view, uint32_t size, T& value) noexcept {
    static_assert(std::is_unsigned<T>::value, "T must be unsigned");
    if (size == 0 || size > sizeof(T) || view.size() < size) {
        return false;
    }
    value = 0;
    for (uint32_t i = 0; i < size; ++i) {
        value |= static_cast<T>(view[i]) << (8 * (size - 1 - i));
    }
    return true;
}

bool readByte(DataView view, uint8_t& value) noexcept {
    return readLE<uint8_t>(view, 1, value);
}

bool read2ByteBE(DataView view, uint16_t& value) noexcept {
    return readBE<uint16_t>(view, 2, value);
}

bool read3ByteBE(DataView view, uint32_t& value) noexcept {
    return readBE<uint32_t>(view, 3, value);
}

bool read4ByteBE(DataView view, uint32_t& value) noexcept {
    return readBE<uint32_t>(view, 4, value);
}

bool read8ByteBE(DataView view, uint64_t& value) noexcept {
    return readBE<uint64_t>(view, 8, value);
}

bool read2ByteLE(DataView view, uint16_t& value) noexcept {
    return readLE<uint16_t>(view, 2, value);
}

bool read3ByteLE(DataView view, uint32_t& value) noexcept {
    return readLE<uint32_t>(view, 3, value);
}

bool read4ByteLE(DataView view, uint32_t& value) noexcept {
    return readLE<uint32_t>(view, 4, value);
}

bool read8ByteLE(DataView view, uint32_t& value) noexcept {
    return readLE<uint32_t>(view, 8, value);
}

bool readBE(DataView view, uint32_t size, uint32_t& value) noexcept {
    return readBE<uint32_t>(view, size, value);
}

bool readLE(DataView view, uint32_t size, uint32_t& value) noexcept {
    return readLE<uint32_t>(view, size, value);
}

template<typename T>
std::string toStringLE(T value) noexcept {
    static_assert(std::is_unsigned<T>::value, "T must be unsigned");
    std::string ret(sizeof(T), '\0');
    for (size_t i = 0; i < sizeof(T); ++i) {
        ret[i] = static_cast<char>((value >> (8 * i)) & 0xFF);
    }
    return ret;
}

template<typename T>
std::string toStringBE(T value) noexcept {
    static_assert(std::is_unsigned<T>::value, "T must be unsigned");
    std::string ret(sizeof(T), '\0');
    for (size_t i = 0; i < sizeof(T); ++i) {
        ret[i] = static_cast<char>((value >> (8 * (sizeof(T) - 1 - i))) & 0xFF);
    }
    return ret;
}

std::string uint32ToStringLE(uint32_t value) noexcept {
    return toStringLE<uint32_t>(value);
}

std::string uint32ToStringBE(uint32_t value) noexcept {
    return toStringBE<uint32_t>(value);
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

