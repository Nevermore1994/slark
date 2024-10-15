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

bool readByte(std::string_view view, uint8_t& value) noexcept {
    if (view.empty()) {
        return false;
    }
    auto func = [&](size_t pos) {
        return static_cast<uint8_t>(view[pos]);
    };
    value = static_cast<uint8_t>(func(0));
    return true;
}

bool read2ByteBE(std::string_view view, uint16_t& value) noexcept {
    if (view.size() < 2) {
        return false;
    }
    auto func = [&](size_t pos) {
        return static_cast<uint8_t>(view[pos]) << ((1 - pos) * 8);
    };
    value = static_cast<uint16_t>(func(0) | func(1));
    return true;
}

bool read3ByteBE(std::string_view view, uint32_t& value) noexcept {
    if (view.size() < 3) {
        return false;
    }
    auto func = [&](size_t pos) {
        return static_cast<uint8_t>(view[pos]) << ((2 - pos) * 8);
    };
    value = static_cast<uint32_t>(func(0) | func(1) | func(2));
    return true;
}

bool read4ByteBE(std::string_view view, uint32_t& value) noexcept {
    if (view.size() < 4) {
        return false;
    }
    auto func = [&](size_t pos) {
        //This must be uint8_t for conversion, char -> uint8_t
        return static_cast<uint8_t>(view[pos]) << ((3 - pos) * 8);
    };
    value = static_cast<uint32_t>(func(0) | func(1) | func(2) | func(3));
    return true;
}

bool read8ByteBE(std::string_view view, uint64_t& value) noexcept {
    if (view.size() < 8) {
        return false;
    }
    auto func = [&](size_t pos) {
        return static_cast<uint64_t>(static_cast<uint8_t>(view[pos]) << ((7 - pos) * 8));
    };
    value = static_cast<uint64_t>(func(0) | func(1) | func(2) | func(3) | func(4) | func(5) | func(6) | func(7));
    return true;
}

bool read2ByteLE(std::string_view view, uint16_t& value) noexcept {
    if (view.size() < 2) {
        return false;
    }
    auto func = [&](size_t pos) {
        return static_cast<uint8_t>(view[pos]) << (pos * 8);
    };
    value = static_cast<uint16_t>(func(1) | func(0));
    return true;
}

bool read3ByteLE(std::string_view view, uint32_t& value) noexcept {
    if (view.size() < 3) {
        return false;
    }
    auto func = [&](size_t pos) {
        return static_cast<uint8_t>(view[pos]) << (pos * 8);
    };
    value = static_cast<uint32_t>(func(2) | func(1) | func(0));
    return true;
}

bool read4ByteLE(std::string_view view, uint32_t& value) noexcept {
    if (view.size() < 4) {
        return false;
    }
    auto func = [&](size_t pos) {
        //This must be uint8_t for conversion, char -> uint8_t
        return static_cast<uint8_t>(view[pos]) << (pos * 8);
    };
    value = static_cast<uint32_t>(func(3) | func(2) | func(1) | func(0));
    return true;
}

bool read8ByteLE(std::string_view view, uint64_t& value) noexcept {
    auto func = [&](size_t pos) {
        return static_cast<uint64_t>(static_cast<uint8_t>(view[pos]) << (pos * 8));
    };
    return static_cast<uint64_t>(func(7) | func(6) | func(5) | func(4) | func(3) | func(2) | func(1) | func(0));
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

}

