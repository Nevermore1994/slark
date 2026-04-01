//
// Created by Nevermore on 2025/4/21.
// slark RingBuffer
// Copyright (c) 2025 Nevermore All rights reserved.
//
#pragma once

#include <algorithm>
#include <cstdint>
#include <mutex>
#include <span>
#include <vector>

namespace slark {

constexpr uint32_t kDefaultRingBufferCapacity = 1024 * 4;

template <typename T>
class RingBuffer {
public:
    explicit RingBuffer(uint32_t capacity = kDefaultRingBufferCapacity)
        : capacity_(capacity), data_(capacity) {}

    RingBuffer(const RingBuffer&) = delete;
    RingBuffer& operator=(const RingBuffer&) = delete;
    RingBuffer(RingBuffer&&) = delete;
    RingBuffer& operator=(RingBuffer&&) = delete;
    ~RingBuffer() = default;

    uint32_t write(const T* src, uint32_t count) noexcept {
        if (src == nullptr || count == 0 || isFull()) {
            return 0;
        }
        count = std::min(count, available());
        copyToRing(src, count);
        size_ += count;
        return count;
    }

    uint32_t write(std::span<const T> src) noexcept {
        return write(src.data(), static_cast<uint32_t>(src.size()));
    }

    uint32_t read(T* dst, uint32_t count) noexcept {
        if (dst == nullptr || count == 0 || isEmpty()) {
            return 0;
        }
        count = std::min(count, size_);
        copyFromRing(dst, count);
        size_ -= count;
        return count;
    }

    uint32_t read(std::span<T> dst) noexcept {
        return read(dst.data(), static_cast<uint32_t>(dst.size()));
    }

    uint32_t peek(T* dst, uint32_t count) const noexcept {
        if (dst == nullptr || count == 0 || isEmpty()) {
            return 0;
        }
        count = std::min(count, size_);
        copyFromRingAt(readPos_, dst, count);
        return count;
    }

    uint32_t peek(std::span<T> dst) const noexcept {
        return peek(dst.data(), static_cast<uint32_t>(dst.size()));
    }

    bool writeExact(const T* src, uint32_t count) noexcept {
        if (count == 0) {
            return true;
        }
        if (src == nullptr || available() < count) {
            return false;
        }
        copyToRing(src, count);
        size_ += count;
        return true;
    }

    bool writeExact(std::span<const T> src) noexcept {
        return writeExact(src.data(), static_cast<uint32_t>(src.size()));
    }

    bool readExact(T* dst, uint32_t count) noexcept {
        if (count == 0) {
            return true;
        }
        if (dst == nullptr || size_ < count) {
            return false;
        }
        copyFromRing(dst, count);
        size_ -= count;
        return true;
    }

    bool readExact(std::span<T> dst) noexcept {
        return readExact(dst.data(), static_cast<uint32_t>(dst.size()));
    }

    uint32_t skip(uint32_t count) noexcept {
        count = std::min(count, size_);
        readPos_ = (readPos_ + count) % capacity_;
        size_ -= count;
        return count;
    }

    /// @return discarded element count
    uint32_t reset() noexcept {
        auto discarded = size_;
        readPos_ = 0;
        writePos_ = 0;
        size_ = 0;
        return discarded;
    }

    [[nodiscard]] bool isEmpty() const noexcept {
        return size_ == 0;
    }

    [[nodiscard]] bool isFull() const noexcept {
        return size_ == capacity_;
    }

    [[nodiscard]] uint32_t length() const noexcept {
        return size_;
    }

    [[nodiscard]] uint32_t available() const noexcept {
        return capacity_ - size_;
    }

    [[nodiscard]] uint32_t capacity() const noexcept {
        return capacity_;
    }

private:
    void copyToRing(const T* src, uint32_t count) noexcept {
        auto firstPart = std::min(count, capacity_ - writePos_);
        std::copy_n(src, firstPart, data_.data() + writePos_);
        if (firstPart < count) {
            std::copy_n(src + firstPart, count - firstPart, data_.data());
        }
        writePos_ = (writePos_ + count) % capacity_;
    }

    void copyFromRing(T* dst, uint32_t count) noexcept {
        copyFromRingAt(readPos_, dst, count);
        readPos_ = (readPos_ + count) % capacity_;
    }

    void copyFromRingAt(uint32_t pos, T* dst, uint32_t count) const noexcept {
        auto firstPart = std::min(count, capacity_ - pos);
        std::copy_n(data_.data() + pos, firstPart, dst);
        if (firstPart < count) {
            std::copy_n(data_.data(), count - firstPart, dst + firstPart);
        }
    }

private:
    uint32_t capacity_;
    std::vector<T> data_;
    uint32_t readPos_ = 0;
    uint32_t writePos_ = 0;
    uint32_t size_ = 0;
};

/// 线程安全的 RingBuffer，通过组合 + mutex 实现
template <typename T>
class SyncRingBuffer {
public:
    explicit SyncRingBuffer(uint32_t capacity = kDefaultRingBufferCapacity) : ring_(capacity) {}

    SyncRingBuffer(const SyncRingBuffer&) = delete;
    SyncRingBuffer& operator=(const SyncRingBuffer&) = delete;
    SyncRingBuffer(SyncRingBuffer&&) = delete;
    SyncRingBuffer& operator=(SyncRingBuffer&&) = delete;
    ~SyncRingBuffer() = default;

    uint32_t write(const T* src, uint32_t count) noexcept {
        std::lock_guard lock(mutex_);
        return ring_.write(src, count);
    }

    uint32_t write(std::span<const T> src) noexcept {
        std::lock_guard lock(mutex_);
        return ring_.write(src);
    }

    uint32_t read(T* dst, uint32_t count) noexcept {
        std::lock_guard lock(mutex_);
        return ring_.read(dst, count);
    }

    uint32_t read(std::span<T> dst) noexcept {
        std::lock_guard lock(mutex_);
        return ring_.read(dst);
    }

    uint32_t peek(T* dst, uint32_t count) const noexcept {
        std::lock_guard lock(mutex_);
        return ring_.peek(dst, count);
    }

    uint32_t peek(std::span<T> dst) const noexcept {
        std::lock_guard lock(mutex_);
        return ring_.peek(dst);
    }

    bool writeExact(const T* src, uint32_t count) noexcept {
        std::lock_guard lock(mutex_);
        return ring_.writeExact(src, count);
    }

    bool writeExact(std::span<const T> src) noexcept {
        std::lock_guard lock(mutex_);
        return ring_.writeExact(src);
    }

    bool readExact(T* dst, uint32_t count) noexcept {
        std::lock_guard lock(mutex_);
        return ring_.readExact(dst, count);
    }

    bool readExact(std::span<T> dst) noexcept {
        std::lock_guard lock(mutex_);
        return ring_.readExact(dst);
    }

    uint32_t skip(uint32_t count) noexcept {
        std::lock_guard lock(mutex_);
        return ring_.skip(count);
    }

    uint32_t reset() noexcept {
        std::lock_guard lock(mutex_);
        return ring_.reset();
    }

    [[nodiscard]] bool isEmpty() const noexcept {
        std::lock_guard lock(mutex_);
        return ring_.isEmpty();
    }

    [[nodiscard]] bool isFull() const noexcept {
        std::lock_guard lock(mutex_);
        return ring_.isFull();
    }

    [[nodiscard]] uint32_t length() const noexcept {
        std::lock_guard lock(mutex_);
        return ring_.length();
    }

    [[nodiscard]] uint32_t available() const noexcept {
        std::lock_guard lock(mutex_);
        return ring_.available();
    }

    [[nodiscard]] uint32_t capacity() const noexcept {
        return ring_.capacity();
    }

    template <typename F>
    auto withLock(F&& func) noexcept(noexcept(func(std::declval<RingBuffer<T>&>())))
        -> decltype(func(std::declval<RingBuffer<T>&>())) {
        std::lock_guard lock(mutex_);
        return std::forward<F>(func)(ring_);
    }

    template <typename F>
    auto withLock(F&& func) const noexcept(noexcept(func(std::declval<const RingBuffer<T>&>())))
        -> decltype(func(std::declval<const RingBuffer<T>&>())) {
        std::lock_guard lock(mutex_);
        return std::forward<F>(func)(ring_);
    }

private:
    RingBuffer<T> ring_;
    mutable std::mutex mutex_;
};

} // namespace slark