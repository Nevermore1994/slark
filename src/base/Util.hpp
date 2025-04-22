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
    if (auto p = ptr.lock()) {
        return std::hash<std::shared_ptr<T>>()(p);
    }
    return std::unexpected(false);
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

[[maybe_unused]] std::string uint32ToStringLE(uint32_t value) noexcept;

[[maybe_unused]] std::string uint32ToStringBE(uint32_t value) noexcept;

struct Golomb {
    static bool readBit(DataView view, int32_t& offset) noexcept;
    
    static uint32_t readBits(DataView view, int32_t& offset, int32_t numBits) noexcept;
    
    static uint32_t readUe(DataView view, int32_t& offset) noexcept;
};

}


