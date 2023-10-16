//
// Created by Nevermore on 2022/5/12.
// slark Data
// Copyright (c) 2022 Nevermore All rights reserved.
//
#pragma once

#include <cstdint>
#include <memory>
#include <algorithm>
#include <string_view>

namespace slark {

struct Data {
    uint64_t capacity = 0;
    uint64_t length = 0;
    uint8_t* rawData = nullptr;

    Data()
        : capacity(0)
        , length(0)
        , rawData(nullptr) {

    }

    Data(uint8_t* d, uint64_t size)
        : capacity(size)
        , length(size)
        , rawData(nullptr) {
        rawData = new (std::nothrow) uint8_t[length];
        std::copy(d, d + size + 1, rawData);
    }

    explicit Data(uint64_t size)
        : capacity(size)
        , length(0)
        , rawData(nullptr) {
        rawData = new (std::nothrow) uint8_t[size];
        std::fill_n(rawData, size, 0);
    }

    ~Data() {
        destroy();
    }

    [[nodiscard]] inline std::unique_ptr<Data> copy() const noexcept {
        return this->copy(0, length);
    }

    [[nodiscard]] inline std::unique_ptr<Data> copy(uint64_t pos, uint64_t len) const noexcept {
        auto res = std::make_unique<Data>();
        if (empty() || len == 0 || (len + pos) > length) {
            return res;
        }
        auto size = len - pos + 1;
        res->capacity = size;
        res->length = size;
        res->rawData = new (std::nothrow) uint8_t[size];
        std::copy(rawData + pos, rawData + pos + len, res->rawData);
        return res;
    }

    inline void destroy() noexcept {
        if (rawData) {
            delete [] rawData;
            rawData = nullptr;
        }
        length = 0;
        capacity = 0;
    }

    [[maybe_unused]]
    inline void resetData() const noexcept {
        if (rawData) {
            std::fill_n(rawData, capacity, 0);
        }
    }

    inline void append(const Data& d) noexcept {
        auto expectLength = length + d.length;
        if (capacity < expectLength) {
            auto p = rawData;
            auto len = static_cast<int64_t>(static_cast<float>(expectLength) * 1.5f);
            rawData = new (std::nothrow) uint8_t[static_cast<size_t>(len)];
            capacity = static_cast<uint64_t>(len);
            if (p) {
                std::copy(p, p + length, rawData);
                delete[] p;
            }
        }
        std::copy(d.rawData, d.rawData + d.length, rawData + length);
        length = expectLength;
    }

    inline void append(std::unique_ptr<Data> appendData) noexcept {
        append(*appendData);
        appendData.reset();
    }

    [[nodiscard]] inline bool empty() const noexcept {
        return length == 0;
    }

    [[nodiscard]] inline std::string_view view() const noexcept {
        return {reinterpret_cast<char*>(rawData), length};
    }
};

}
