//
// Created by Nevermore on 2021/11/20.
// slark Buffer
// Copyright (c) 2021 Nevermore All rights reserved.
//
#pragma once

#include <cstdint>
#include <array>
#include <algorithm>
#include <memory>
#include "NonCopyable.h"

namespace slark {

constexpr uint32_t RingBufferLength = 1024 * 1024 * 500;

template<typename T, uint32_t Capacity = RingBufferLength>
class RingBuffer : public NonCopyable {
public:
    RingBuffer()
        : data_(std::make_unique<std::array<T, Capacity>>()) {

    }

    ~RingBuffer() override = default;

public:
    void append(const T* data, uint32_t size) noexcept {
        if (size == 0 || isFull()) return;

        size = std::min(size, Capacity - size_); // 确保不会超过可用空间

        auto firstPart = std::min(size, Capacity - writePos_);
        std::copy(data, data + firstPart, data_->data() + writePos_);

        writePos_ = (writePos_ + firstPart) % Capacity;

        if (size > firstPart) {
            auto secondPart = size - firstPart;
            std::copy(data + firstPart, data + firstPart + secondPart, data_->data());
            writePos_ = secondPart;
        }
        size_ += size;
        readSize_ += size;
    }

    void reset() noexcept {
        std::fill(data_->data(), data_->data() + Capacity, T{});
        writePos_ = 0;
        readPos_ = 0;
        size_ = 0;
        readSize_ = 0;
    }

    uint32_t read(T* data, uint32_t size) noexcept {
        if (size == 0 || isEmpty()) return 0;

        size = std::min(size, length());  // 只读取可用数据的大小

        auto firstPart = std::min(size, Capacity - readPos_);
        std::copy(data_->data() + readPos_, data_->data() + readPos_ + firstPart, data);

        readPos_ = (readPos_ + firstPart) % Capacity;

        if (size > firstPart && size_ > 0) {
            auto secondPart = std::min(size - firstPart, size_);
            std::copy(data_->data(), data_->data() + secondPart, data + firstPart);
            readPos_ = secondPart;
        }
        size_ -= size;
        return size;
    }

    [[nodiscard]] uint32_t tail() noexcept {
        return Capacity - size_;
    }

    [[nodiscard]] uint32_t capacity() noexcept {
        return Capacity;
    }

    [[nodiscard]] inline uint32_t length() noexcept {
        return size_;
    }

    [[nodiscard]] bool isEmpty() noexcept {
        return size_ == 0;
    }

    [[nodiscard]] bool isFull() noexcept {
        return size_ == Capacity;
    }

    [[nodiscard]] uint32_t readSize() const noexcept {
        return readSize_;
    }

private:
    std::unique_ptr<std::array<T, Capacity>> data_;
    uint32_t readPos_ = 0;
    uint32_t writePos_ = 0;
    uint32_t size_ = 0;
    uint32_t readSize_ = 0;
};

}



