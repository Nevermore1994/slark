//
// Created by Nevermore on 2025/4/21.
// test RingBufferImpl
// Copyright (c) 2025 Nevermore All rights reserved.
//
#pragma once

#include <cstdint>
#include <array>
#include <algorithm>
#include <memory>
#include <cassert>
#include <atomic>
#include <mutex>
#include "NonCopyable.h"

namespace slark {

constexpr uint32_t kDefaultRingBufferLength = 1024 * 4;

template<typename T>
class RingBufferImpl: public NonCopyable {
public:
    [[nodiscard]] uint32_t capacity() const noexcept {
        return capacity_;
    }

protected:
    explicit RingBufferImpl(uint32_t capacity)
            : capacity_(capacity)
            , data_(std::make_unique<std::vector<T>>(capacity)) {

    }

    ~RingBufferImpl() override = default;

    uint32_t writeInternal(const T* data, uint32_t size) noexcept {
        if (data == nullptr || size == 0 || isFullInternal()) {
            return 0;
        }

        size = std::min(size, tailInternal());
        const uint32_t endPos = writePos_ + size;

        if (endPos <= capacity_) {
            std::copy_n(data, size, data_->data() + writePos_);
            writePos_ = (endPos == capacity_) ? 0 : endPos;
        } else {
            const uint32_t firstPart = capacity_ - writePos_;
            std::copy_n(data, firstPart, data_->data() + writePos_);
            std::copy_n(data + firstPart, size - firstPart, data_->data());
            writePos_ = size - firstPart;
        }

        size_ += size;
        return size;
    }

    uint32_t readInternal(T* data, uint32_t size) noexcept {
        if (data == nullptr || size == 0 || isEmptyInternal()) {
            return 0;
        }

        size = std::min(size, size_);
        const uint32_t endPos = readPos_ + size;

        if (endPos <= capacity_) {
            std::copy_n(data_->data() + readPos_, size, data);
            readPos_ = (endPos == capacity_) ? 0 : endPos;
        } else {
            const uint32_t firstPart = capacity_ - readPos_;
            std::copy_n(data_->data() + readPos_, firstPart, data);
            std::copy_n(data_->data(), size - firstPart, data + firstPart);
            readPos_ = size - firstPart;
        }

        size_ -= size;
        return size;
    }

    [[nodiscard]] bool isEmptyInternal() const noexcept { return size_ == 0; }
    [[nodiscard]] bool isFullInternal() const noexcept { return size_ == capacity_; }
    [[nodiscard]] uint32_t tailInternal() const noexcept { return capacity_ - size_; }
    [[nodiscard]] uint32_t lengthInternal() const noexcept { return size_; }

    uint32_t resetInternal() noexcept {
        readPos_ = 0;
        writePos_ = 0;
        auto discard = size_;
        size_ = 0;
        return discard;
    }
protected:
    uint32_t capacity_ = kDefaultRingBufferLength;
    std::unique_ptr<std::vector<T>> data_;
    uint32_t readPos_ = 0;
    uint32_t writePos_ = 0;
    uint32_t size_ = 0;
};

template<typename T>
class RingBuffer : public RingBufferImpl<T> {
public:
    using RingBufferImpl<T>::RingBufferImpl;

    explicit RingBuffer(uint32_t capacity = kDefaultRingBufferLength)
        : RingBufferImpl<T>(capacity) {

    }

    ~RingBuffer() override = default;

    uint32_t write(const T* data, uint32_t size) noexcept {
        return this->writeInternal(data, size);
    }

    uint32_t read(T* data, uint32_t size) noexcept {
        return this->readInternal(data, size);
    }

    bool writeExact(const T* data, uint32_t size) noexcept {
        return (size == 0) || (this->writeInternal(data, size) == size);
    }

    bool readExact(T* data, uint32_t size) noexcept {
        return (size == 0) || (this->readInternal(data, size) == size);
    }

    [[nodiscard]] bool isEmpty() const noexcept {
        return this->isEmptyInternal();
    }

    [[nodiscard]] bool isFull() const noexcept {
        return this->isFullInternal();
    }

    [[nodiscard]] uint32_t tail() const noexcept {
        return this->tailInternal();
    }

    [[nodiscard]] uint32_t length() const noexcept {
        return this->lengthInternal();
    }

    ///return discard size
    uint32_t reset() noexcept {
        return this->resetInternal();
    }
private:
};

template<typename T>
class SyncRingBuffer : public RingBufferImpl<T> {
public:
    using RingBufferImpl<T>::RingBufferImpl;
    explicit SyncRingBuffer(uint32_t capacity = kDefaultRingBufferLength)
        : RingBufferImpl<T>(capacity) {

    }

    ~SyncRingBuffer() override = default;

    uint32_t write(const T* data, uint32_t size) noexcept {
        std::lock_guard<std::mutex> lock(mutex_);
        return this->writeInternal(data, size);
    }

    uint32_t read(T* data, uint32_t size) noexcept {
        std::lock_guard<std::mutex> lock(mutex_);
        return this->readInternal(data, size);
    }

    bool writeExact(const T* data, uint32_t size) noexcept {
        std::lock_guard<std::mutex> lock(mutex_);
        if (this->tailInternal() < size) {
            return false;
        }
        return (size == 0) || (this->writeInternal(data, size) == size);
    }

    bool readExact(T* data, uint32_t size) noexcept {
        std::lock_guard<std::mutex> lock(mutex_);
        if (this->lengthInternal() < size) {
            return false;
        }
        return (size == 0) || (this->readInternal(data, size) == size);
    }

    [[nodiscard]] bool isEmpty() const noexcept {
        std::lock_guard<std::mutex> lock(mutex_);
        return this->isEmptyInternal();
    }

    [[nodiscard]] bool isFull() const noexcept {
        std::lock_guard<std::mutex> lock(mutex_);
        return this->isFullInternal();
    }

    [[nodiscard]] uint32_t tail() const noexcept {
        std::lock_guard<std::mutex> lock(mutex_);
        return this->tailInternal();
    }

    [[nodiscard]] uint32_t length() const noexcept {
        std::lock_guard<std::mutex> lock(mutex_);
        return this->lengthInternal();
    }

    ///return discard size
    uint32_t reset() noexcept {
        std::lock_guard<std::mutex> lock(mutex_);
        return this->resetInternal();
    }
private:
    mutable std::mutex mutex_;
};

} // namespace slark