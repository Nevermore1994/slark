//
//  Util.cpp
//  slark
//
//  Created by Nevermore on 2023/10/20.
//  Copyright (c) 2023 Nevermore All rights reserved.

#include "Util.hpp"
#include "Random.hpp"
#include "Assert.hpp"

namespace slark::Util {

std::string genRandomName(const std::string& namePrefix) noexcept {
    return namePrefix + Random::randomString(8);
}

uint8_t readByteBE(std::string_view view) {
    SAssert(!view.empty(), "out of range");
    auto func = [&](size_t pos) {
        return static_cast<uint8_t>(view[pos]) << (pos * 8);
    };
    return static_cast<uint8_t>(func(0));
}

uint16_t read2ByteBE(std::string_view view) {
    SAssert(view.size() >= 2, "out of range");
    auto func = [&](size_t pos) {
        return static_cast<uint8_t>(view[pos]) << ((1 - pos) * 8);
    };
    return static_cast<uint16_t>(func(0) | func(1));
}

uint32_t read3ByteBE(std::string_view view) {
    SAssert(view.size() >= 3, "out of range");
    auto func = [&](size_t pos) {
        return static_cast<uint8_t>(view[pos]) << ((2 - pos) * 8);
    };
    return static_cast<uint32_t>(func(0) | func(1) | func(2));
}

uint32_t read4ByteBE(std::string_view view) {
    SAssert(view.size() >= 4, "out of range");
    auto func = [&](size_t pos) {
        //This must be uint8_t for conversion, char -> uint8_t
        return static_cast<uint8_t>(view[pos]) << ((3 - pos) * 8);
    };
    return static_cast<uint32_t>(func(0) | func(1) | func(2) | func(3));
}

uint8_t readByteLE(std::string_view view) {
    SAssert(!view.empty(), "out of range");
    auto func = [&](size_t pos) {
        return static_cast<uint8_t>(view[pos]) << (pos * 8);
    };
    return static_cast<uint8_t>(func(0));
}

uint16_t read2ByteLE(std::string_view view) {
    SAssert(view.size() >= 2, "out of range");
    auto func = [&](size_t pos) {
        return static_cast<uint8_t>(view[pos]) << (pos * 8);
    };
    return static_cast<uint16_t>(func(1) | func(0));
}

uint32_t read3ByteLE(std::string_view view) {
    SAssert(view.size() >= 3, "out of range");
    auto func = [&](size_t pos) {
        return static_cast<uint8_t>(view[pos]) << (pos * 8);
    };
    return static_cast<uint32_t>(func(2) | func(1) | func(0));
}

uint32_t read4ByteLE(std::string_view view) {
    SAssert(view.size() >= 4, "out of range");
    auto func = [&](size_t pos) {
        //This must be uint8_t for conversion, char -> uint8_t
        return static_cast<uint8_t>(view[pos]) << (pos * 8);
    };
    return static_cast<uint32_t>(func(3) | func(2) | func(1) | func(0));
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

