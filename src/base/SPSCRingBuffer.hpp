#pragma once

#include "RingBufferUtil.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <cstdint>
#include <type_traits>

namespace slark {

template <typename T, size_t N>
    requires std::is_trivially_copyable_v<T>
class SPSCRingBuffer {
    // 牺牲一个槽位区分空/满，实际可用容量 = kCapacity - 1
    static constexpr size_t kCapacity = std::bit_ceil((N == 0) ? 2 : (N + 1));
    static constexpr size_t kMask = kCapacity - 1;

public:
    SPSCRingBuffer() = default;
    SPSCRingBuffer(const SPSCRingBuffer&) = delete;
    SPSCRingBuffer& operator=(const SPSCRingBuffer&) = delete;
    ~SPSCRingBuffer() = default;

    bool writeExact(const T* src, uint32_t count) noexcept {
        if (count == 0) {
            return true;
        }
        if (src == nullptr || available() < count) {
            return false;
        }
        auto wp = writePos_.load(std::memory_order_relaxed);
        detail::wrapCopy(src, data_.data(), kCapacity, wp, count);
        writePos_.store((wp + count) & kMask, std::memory_order_release);
        return true;
    }

    bool readExact(T* dst, uint32_t count) noexcept {
        if (count == 0) {
            return true;
        }
        if (dst == nullptr || length() < count) {
            return false;
        }
        auto rp = readPos_.load(std::memory_order_relaxed);
        detail::wrapCopyFrom(data_.data(), kCapacity, rp, dst, count);
        readPos_.store((rp + count) & kMask, std::memory_order_release);
        return true;
    }

    void reset() noexcept {
        readPos_.store(0, std::memory_order_release);
        writePos_.store(0, std::memory_order_release);
    }

    [[nodiscard]] uint32_t length() const noexcept {
        auto wp = writePos_.load(std::memory_order_acquire);
        auto rp = readPos_.load(std::memory_order_acquire);
        return static_cast<uint32_t>((wp - rp + kCapacity) & kMask);
    }

    [[nodiscard]] bool isEmpty() const noexcept {
        return writePos_.load(std::memory_order_acquire) ==
               readPos_.load(std::memory_order_acquire);
    }

    [[nodiscard]] bool isFull() const noexcept {
        auto wp = writePos_.load(std::memory_order_acquire);
        auto rp = readPos_.load(std::memory_order_acquire);
        return ((wp + 1) & kMask) == rp;
    }

    [[nodiscard]] uint32_t available() const noexcept {
        return capacity() - length();
    }

    [[nodiscard]] uint32_t capacity() const noexcept {
        return static_cast<uint32_t>(data_.size()) - 1;
    }

private:
    std::array<T, kCapacity> data_;
    alignas(64) std::atomic<uint32_t> readPos_ = 0;
    alignas(64) std::atomic<uint32_t> writePos_ = 0;
};

} // namespace slark
