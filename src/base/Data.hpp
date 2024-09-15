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
#include <string>
#include <functional>

namespace slark {

struct Data;
using DataPtr = std::unique_ptr<Data>;
using DataRefPtr = std::shared_ptr<Data>;
using DataView = std::string_view;

struct Data {
    uint64_t capacity = 0;
    uint64_t length = 0;
    uint8_t* rawData = nullptr;

    Data()
        : capacity(0)
        , length(0)
        , rawData(nullptr) {

    }

    Data(uint64_t size, const uint8_t* data)
        : capacity(size)
        , length(size)
        , rawData(nullptr) {
        rawData = new uint8_t[length];
        std::copy(data, data + size, rawData);
    }

    explicit Data(uint64_t size)
        : capacity(size)
        , length(0)
        , rawData(nullptr) {
        rawData = new uint8_t[size];
        std::fill_n(rawData, size, 0);
    }

    explicit Data(uint64_t size, const std::function<void(uint8_t*)>& func)
        : capacity(size)
        , length(0)
        , rawData(nullptr) {
        rawData = new uint8_t[size];
        if (func) {
            func(rawData);
        }
    }

    ~Data() {
        destroy();
    }

    Data(const Data& data)
        : capacity (data.capacity)
        , length (data.length)
        , rawData(nullptr) {
        rawData = new uint8_t[data.capacity];
        std::copy(data.rawData, data.rawData + data.length, rawData);
    }

    Data(Data&& data) noexcept
        : capacity (data.capacity)
        , length (data.length)
        , rawData(data.rawData) {
        data.rawData = nullptr;
        data.length = 0;
        data.capacity = 0;
    }

    Data& operator=(const Data& data) {
        if (&data == this) {
            return *this;
        }
        destroy();
        rawData = new uint8_t[data.capacity];
        capacity = data.capacity;
        length = data.length;
        std::copy(data.rawData, data.rawData + data.length, rawData);
        return *this;
    }

    Data& operator=(Data&& data) noexcept {
        if (&data == this) {
            return *this;
        }
        destroy();
        capacity = data.capacity;
        length = data.length;
        rawData = data.rawData;
        data.rawData = nullptr;
        return *this;
    }

#pragma clang diagnostic push
#pragma ide diagnostic ignored "google-explicit-constructor"
    Data(const std::string& str)
        : Data(static_cast<uint64_t>(str.size()), reinterpret_cast<const uint8_t*>(str.data())) {

    }

    Data(std::string&& str)
        : Data(static_cast<uint64_t>(str.size()), reinterpret_cast<const uint8_t*>(str.data())) {

    }
#pragma clang diagnostic pop

    [[nodiscard]] inline DataPtr copy() const noexcept {
        return this->copy(0, static_cast<int64_t>(length));
    }

    [[nodiscard]] inline DataPtr copy(uint64_t pos, int64_t len = -1) const noexcept {
        auto res = std::make_unique<Data>();
        if (empty() || len == 0 || pos >= length) {
            return res;
        }
        auto resLen = static_cast<uint64_t>(len);
        if (len < 0) {
            resLen = length - pos;
        }
        auto size = std::min(static_cast<uint64_t>(resLen), length - pos);
        res->capacity = size;
        res->length = size;
        res->rawData = new uint8_t[size];
        std::copy(rawData + pos, rawData + std::min(pos + resLen, length), res->rawData);
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
    inline void resetData() noexcept {
        if (rawData) {
            std::fill_n(rawData, capacity, 0);
        }
        length = 0;
    }

    [[maybe_unused]]
    inline void resize(uint64_t size) noexcept {
        if (size == 0) {
            destroy();
            return;
        }
        if (size == capacity) {
            return;
        }
        auto p = rawData;
        rawData = new uint8_t [size];
        auto contentLength = std::min(size, length);
        std::copy(p, p + contentLength, rawData);
        length = contentLength;
        capacity = size;
        delete[] p;
    }

    inline DataPtr detachData() {
        auto res = std::make_unique<Data>();
        res->length = length;
        res->capacity = capacity;
        res->rawData = rawData;
        length = 0;
        capacity = 0;
        rawData = nullptr;
        return res;
    }

    inline void append(const Data& d) noexcept {
        if (rawData == nullptr) {
            length = 0;
            capacity = 0;
        }
        auto expectLength = length + d.length;
        if (capacity < expectLength) {
            auto p = rawData;
            auto len = static_cast<int64_t>(static_cast<float>(expectLength) * 1.5f);
            rawData = new uint8_t[static_cast<size_t>(len)];
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

    inline Data operator+(const Data& data) noexcept {
        Data res = *this;
        res.append(data);
        return res;
    }

    inline Data& operator+=(const Data& data) noexcept {
        this->append(data);
        return *this;
    }

    [[nodiscard]] inline bool empty() const noexcept {
        return rawData == nullptr || length == 0;
    }

    [[nodiscard]] inline bool isFull() const noexcept {
        return !empty() && length == capacity;
    }

    [[nodiscard]] inline DataView view() const noexcept {
        if (rawData == nullptr) {
            return {};
        }
        return {reinterpret_cast<char*>(rawData), length};
    }

    [[nodiscard]] inline bool operator==(const Data& data) const noexcept {
        return length == data.length && view() == data.view();
    }
}; //end of class Data

} //end of namespace slark