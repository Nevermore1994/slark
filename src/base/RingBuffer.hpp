//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//
#pragma once

#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <span>
#include <type_traits>

#include "RingBufferUtil.hpp"

namespace slark {

namespace detail {

template <typename T>
constexpr size_t calcCapacity(size_t n) noexcept {
    return std::bit_ceil(n);
}

}  // namespace detail

template <typename T, size_t N>
requires std::is_trivially_copyable_v<T> class RingBuffer {
    static constexpr size_t kCapacity = std::bit_ceil(N);
    static constexpr size_t kMask = kCapacity - 1;

 public:
    RingBuffer() = default;
    RingBuffer(const RingBuffer&) = delete;
    RingBuffer& operator=(const RingBuffer&) = delete;
    ~RingBuffer() = default;

    uint32_t write(const T* src, uint32_t count) noexcept {
        if (src == nullptr || count == 0 || isFull()) {
            return 0;
        }
        count = std::min(count, available());
        detail::wrapCopy(src, data_.data(), kCapacity, writePos_, count);
        writePos_ = (writePos_ + count) & kMask;
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
        count = std::min(count, length());
        detail::wrapCopyFrom(data_.data(), kCapacity, readPos_, dst, count);
        readPos_ = (readPos_ + count) & kMask;
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
        count = std::min(count, length());
        detail::wrapCopyFrom(data_.data(), kCapacity, readPos_, dst, count);
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
        detail::wrapCopy(src, data_.data(), kCapacity, writePos_, count);
        writePos_ = (writePos_ + count) & kMask;
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
        if (dst == nullptr || length() < count) {
            return false;
        }
        detail::wrapCopyFrom(data_.data(), kCapacity, readPos_, dst, count);
        readPos_ = (readPos_ + count) & kMask;
        size_ -= count;
        return true;
    }

    bool readExact(std::span<T> dst) noexcept {
        return readExact(dst.data(), static_cast<uint32_t>(dst.size()));
    }

    void reset() noexcept {
        readPos_ = 0;
        writePos_ = 0;
        size_ = 0;
    }

    [[nodiscard]] bool isEmpty() const noexcept {
        return size_ == 0;
    }

    [[nodiscard]] bool isFull() const noexcept {
        return size_ == kCapacity;
    }

    [[nodiscard]] uint32_t length() const noexcept {
        return static_cast<uint32_t>(size_);
    }

    [[nodiscard]] uint32_t available() const noexcept {
        return static_cast<uint32_t>(kCapacity) - static_cast<uint32_t>(size_);
    }

    static constexpr uint32_t capacity() noexcept {
        return static_cast<uint32_t>(kCapacity);
    }

 private:
    std::array<T, kCapacity> data_;
    size_t readPos_ = 0;
    size_t writePos_ = 0;
    size_t size_ = 0;  // 追踪实际数据量，正确区分空/满
};

template <typename T, size_t N>
requires std::is_trivially_copyable_v<T> class SyncRingBuffer {
    static constexpr size_t kCapacity = std::bit_ceil(N);
    static constexpr size_t kMask = kCapacity - 1;

 public:
    SyncRingBuffer() = default;
    SyncRingBuffer(const SyncRingBuffer&) = delete;
    SyncRingBuffer& operator=(const SyncRingBuffer&) = delete;
    ~SyncRingBuffer() = default;

    bool writeExact(const T* src, uint32_t count) noexcept {
        std::lock_guard lock(mutex_);
        if (count == 0) {
            return true;
        }
        if (src == nullptr || availableUnsafe() < count) {
            return false;
        }
        detail::wrapCopy(src, data_.data(), kCapacity, writePos_, count);
        size_ += count;
        writePos_ = (writePos_ + count) & kMask;
        cv_.notify_one();
        return true;
    }

    bool writeExact(std::span<const T> src) noexcept {
        return writeExact(src.data(), static_cast<uint32_t>(src.size()));
    }

    bool writeExact(const T* src, uint32_t count, int64_t timeoutMs) noexcept {
        if (count == 0) {
            return true;
        }
        if (src == nullptr) {
            return false;
        }
        std::unique_lock lock(mutex_);
        if (timeoutMs == 0) {
            if (availableUnsafe() < count) {
                return false;
            }
        } else {
            auto pred = [&] {
                return availableUnsafe() >= count;
            };
            if (timeoutMs < 0) {
                cv_.wait(lock, pred);
            } else if (!cv_.wait_for(lock, std::chrono::milliseconds(timeoutMs), pred)) {
                return false;
            }
        }
        detail::wrapCopy(src, data_.data(), kCapacity, writePos_, count);
        size_ += count;
        writePos_ = (writePos_ + count) & kMask;
        cv_.notify_one();
        return true;
    }

    bool readExact(T* dst, uint32_t count) noexcept {
        std::lock_guard lock(mutex_);
        if (count == 0) {
            return true;
        }
        if (dst == nullptr || size_ < count) {
            return false;
        }
        detail::wrapCopyFrom(data_.data(), kCapacity, readPos_, dst, count);
        size_ -= count;
        readPos_ = (readPos_ + count) & kMask;
        cv_.notify_one();
        return true;
    }

    bool readExact(std::span<T> dst) noexcept {
        return readExact(dst.data(), static_cast<uint32_t>(dst.size()));
    }

    bool readExact(T* dst, uint32_t count, int64_t timeoutMs) noexcept {
        if (count == 0) {
            return true;
        }
        if (dst == nullptr) {
            return false;
        }
        std::unique_lock lock(mutex_);
        if (timeoutMs == 0) {
            if (size_ < count) {
                return false;
            }
        } else {
            auto pred = [&] {
                return size_ >= count;
            };
            if (timeoutMs < 0) {
                cv_.wait(lock, pred);
            } else if (!cv_.wait_for(lock, std::chrono::milliseconds(timeoutMs), pred)) {
                return false;
            }
        }
        detail::wrapCopyFrom(data_.data(), kCapacity, readPos_, dst, count);
        size_ -= count;
        readPos_ = (readPos_ + count) & kMask;
        cv_.notify_one();
        return true;
    }

    uint32_t peek(T* dst, uint32_t count) const noexcept {
        std::lock_guard lock(mutex_);
        if (dst == nullptr || count == 0 || size_ == 0) {
            return 0;
        }
        count = std::min(count, size_);
        detail::wrapCopyFrom(data_.data(), kCapacity, readPos_, dst, count);
        return count;
    }

    uint32_t peek(std::span<T> dst) const noexcept {
        return peek(dst.data(), static_cast<uint32_t>(dst.size()));
    }

    void reset() noexcept {
        std::lock_guard lock(mutex_);
        readPos_ = 0;
        writePos_ = 0;
        size_ = 0;
        cv_.notify_all();
    }

    [[nodiscard]] bool isEmpty() const noexcept {
        std::lock_guard lock(mutex_);
        return size_ == 0;
    }

    [[nodiscard]] bool isFull() const noexcept {
        std::lock_guard lock(mutex_);
        return size_ == kCapacity;
    }

    [[nodiscard]] uint32_t length() const noexcept {
        std::lock_guard lock(mutex_);
        return size_;
    }

    [[nodiscard]] uint32_t available() const noexcept {
        std::lock_guard lock(mutex_);
        return static_cast<uint32_t>(kCapacity) - size_;
    }

    static constexpr uint32_t capacity() noexcept {
        return static_cast<uint32_t>(kCapacity);
    }

    template <typename F>
    auto withLock(F&& func) noexcept(noexcept(func(*this))) -> decltype(func(*this)) {
        std::lock_guard lock(mutex_);
        return std::forward<F>(func)(*this);
    }

    template <typename F>
    auto withLock(F&& func) const noexcept(noexcept(func(*this))) -> decltype(func(*this)) {
        std::lock_guard lock(mutex_);
        return std::forward<F>(func)(*this);
    }

 private:
    uint32_t availableUnsafe() const noexcept {
        return static_cast<uint32_t>(kCapacity) - size_;
    }

    std::array<T, kCapacity> data_;
    mutable std::mutex mutex_;
    std::condition_variable cv_;
    size_t readPos_ = 0;
    size_t writePos_ = 0;
    uint32_t size_ = 0;
};

}  // namespace slark