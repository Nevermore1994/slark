//
// Created by Nevermore on 2021/11/20.
// Slark Buffer
// Copyright (c) 2021 Nevermore All rights reserved.
//
#pragma once

#include "NonCopyable.hpp"
#include <cstdint>
#include <mutex>
#include <array>
#include <atomic>
#include <algorithm>

namespace Slark {

constexpr uint64_t RingBufferLength = 1024 * 1024 * 500;

template<typename T, uint64_t Size = RingBufferLength>
class RingBuffer : public NonCopyable {
public:
    RingBuffer();

    ~RingBuffer() override = default;

public:
    void append(const T* data, uint64_t size) noexcept;

    void reset() noexcept;

    uint64_t read(T* data, uint64_t size) noexcept;

    uint64_t maxSize() const noexcept {
        return size_;
    }

private:
    inline uint64_t length() const noexcept {
        int64_t length = writePosition_ - readPosition_;
        if (length < 0) {
            length += size_;
        }
        return length;
    }

private:
    std::unique_ptr<std::array<T, Size>> data_;
    uint64_t readPosition_ = 0;
    uint64_t writePosition_ = 0;
    uint64_t size_ = Size;
};

template<typename T, uint64_t Size>
RingBuffer<T, Size>::RingBuffer()
    : data_(std::make_unique<std::array<T, Size>>())
    , size_(Size) {

}

template<typename T, uint64_t Size>
void RingBuffer<T, Size>::append(const T* data, uint64_t size) noexcept {
    auto pos = writePosition_ + size;
    if (pos > size_) {
        auto t = size_ - writePosition_;
        std::copy(data, data + t, data_->data() + writePosition_);
        writePosition_ = 0;
        std::copy(data + t, data + size, data_->data() + writePosition_);
        writePosition_ = size - t;
    } else {
        std::copy(data, data + size, data_->data() + writePosition_);
        writePosition_ = pos;
    }
}

template<typename T, uint64_t Size>
void RingBuffer<T, Size>::reset() noexcept {
    if (data_) {
        writePosition_ = 0;
        readPosition_ = 0;
    }
}

template<typename T, uint64_t Size>
uint64_t RingBuffer<T, Size>::read(T* data, uint64_t size) noexcept {
    auto dataLength = length();
    uint32_t readSize = 0;
    if (dataLength == 0) {
        return readSize;
    } else if (dataLength >= size) {
        readSize = size;
    } else {
        readSize = dataLength;
    }

    auto pos = readPosition_ + readSize;
    if (pos < size_) {
        std::copy(data_->data() + readPosition_, data_->data() + readPosition_ + readSize, data);
        readPosition_ += readSize;
    } else {
        auto temp = size_ - readPosition_;
        std::copy(data_->data() + readPosition_, data_->data() + readPosition_ + temp, data);
        readPosition_ = 0;
        std::copy(data_->data() + readPosition_, data_->data() + readPosition_ + readSize - temp, data + temp);
        readPosition_ =  readSize - temp;
    }
    return readSize;
}

}



