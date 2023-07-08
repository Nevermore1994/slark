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
    uint8_t* data = nullptr;

    Data()
        : capacity(0)
        , length(0)
        , data(nullptr) {

    }

    Data(uint8_t* d, uint64_t size)
        : capacity(size)
        , length(size)
        , data(nullptr) {
        data = new (std::nothrow) uint8_t[length];
        std::copy(d, d + size + 1, data);
    }

    explicit Data(uint64_t size)
        : capacity(size)
        , length(0)
        , data(nullptr) {
        data = new (std::nothrow) uint8_t[size];
        std::fill_n(data, size, 0);
    }

    ~Data() {
        release();
    }

    [[nodiscard]] inline std::unique_ptr<Data> copy() const noexcept {
        return this->copy(0, length);
    }

    [[nodiscard]] inline std::unique_ptr<Data> copy(uint64_t pos, uint64_t len) const noexcept {
        auto res = std::make_unique<Data>();
        if (empty() || len == 0 || (len + pos) > length) {
            return res;
        }
        res->capacity = len;
        res->length = len;
        res->data = new (std::nothrow) uint8_t[len];
        std::copy(data + pos, data + pos + len, res->data);
        return res;
    }

    inline void release() noexcept {
        if (data) {
            delete [] data;
            data = nullptr;
        }
        length = 0;
        capacity = 0;
    }

    inline void resetData() const noexcept {
        if (data) {
            std::fill_n(data, length, 0);
        }
    }

    inline void append(const Data& d) noexcept {
        auto expectLength = length + d.length;
        if (capacity < expectLength) {
            auto p = data;
            auto len = static_cast<int64_t>(static_cast<float>(expectLength) * 1.5f);
            data = new (std::nothrow) uint8_t[static_cast<size_t>(len)];
            capacity = static_cast<uint64_t>(len);
            if (p) {
                std::copy(p, p + length, data);
                delete[] p;
            }
        }
        std::copy(d.data, d.data + d.length, data + length);
        length = expectLength;
    }

    inline void append(std::unique_ptr<Data> data) noexcept {
        append(*data);
        data.reset();
    }

    inline bool empty() const noexcept {
        return length == 0;
    }

    inline std::string_view view() const noexcept {
        return {reinterpret_cast<char*>(data), length};
    }
};

}
