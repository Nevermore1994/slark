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

namespace slark::Util {

template <typename T>
std::expected<uint64_t, bool> getWeakPtrAddress(const std::weak_ptr<T>& ptr) noexcept {
    if (ptr.expired()) {
        return std::unexpected(false);
    }
    return reinterpret_cast<uint64_t>(ptr.lock().get());
}

std::string genRandomName(const std::string& namePrefix) noexcept;

bool readByte(std::string_view view, uint8_t& value) noexcept;

bool read2ByteBE(std::string_view view, uint16_t& value) noexcept;

bool read3ByteBE(std::string_view view, uint32_t& value) noexcept;

bool read4ByteBE(std::string_view view, uint32_t& value) noexcept;

bool read8ByteBE(std::string_view view, uint64_t& value) noexcept;

bool read2ByteLE(std::string_view view, uint16_t& value) noexcept;

bool read3ByteLE(std::string_view view, uint32_t& value) noexcept;

bool read4ByteLE(std::string_view view, uint32_t& value) noexcept;

bool read8ByteLE(std::string_view view, uint64_t& value) noexcept;

bool readBE(std::string_view view, uint32_t size, uint32_t& value) noexcept;

std::string uint32ToByteLE(uint32_t value) noexcept;

std::string uint32ToByteBE(uint32_t value) noexcept;

uint32_t readUe(std::string_view view, int32_t& offset);
}


