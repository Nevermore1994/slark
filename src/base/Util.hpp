//
//  Util.hpp
//  slark
//
//  Created by Nevermore on 2023/10/20.
//  Copyright (c) 2023 Nevermore All rights reserved.

#pragma once

#include <memory>
#include <cstdint>
#include <string>
#include <expected>
#include "DataView.h"

namespace slark::Util {

template <typename T>
std::expected<uint64_t, bool> getWeakPtrAddress(const std::weak_ptr<T>& ptr) noexcept {
    if (ptr.expired()) {
        return std::unexpected(false);
    }
    return reinterpret_cast<uint64_t>(ptr.lock().get());
}

std::string genRandomName(const std::string& namePrefix) noexcept;

bool readByte(DataView view, uint8_t& value) noexcept;

bool read2ByteBE(DataView view, uint16_t& value) noexcept;

bool read3ByteBE(DataView view, uint32_t& value) noexcept;

bool read4ByteBE(DataView view, uint32_t& value) noexcept;

bool read8ByteBE(DataView view, uint64_t& value) noexcept;

bool read2ByteLE(DataView view, uint16_t& value) noexcept;

bool read3ByteLE(DataView view, uint32_t& value) noexcept;

bool read4ByteLE(DataView view, uint32_t& value) noexcept;

bool read8ByteLE(DataView view, uint64_t& value) noexcept;

bool readBE(DataView view, uint32_t size, uint32_t& value) noexcept;

bool readLE(DataView view, uint32_t size, uint32_t& value) noexcept;

std::string uint32ToByteLE(uint32_t value) noexcept;

std::string uint32ToByteBE(uint32_t value) noexcept;

struct Golomb {
    static bool readBit(DataView view, int32_t& offset) noexcept;
    
    static uint32_t readBits(DataView view, int32_t& offset, int32_t numBits) noexcept;
    
    static uint32_t readUe(DataView view, int32_t& offset) noexcept;
};

}


