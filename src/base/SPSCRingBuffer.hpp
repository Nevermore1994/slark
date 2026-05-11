//
// Created by Nevermore on 2025/4/21.
// slark SPSCRingBuffer
// Copyright (c) 2025 Nevermore All rights reserved.
//
#pragma once

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <vector>

namespace slark {

/// SPSC lock-free ring buffer: single writer thread (pipeline) + single reader thread (audio
/// callback). Avoids mutex in real-time audio callback threads via atomic read/write positions.
template <typename T>
    requires std::is_trivially_copyable_v<T>
class SPSCRingBuffer {
public:
    explicit SPSCRingBuffer(uint32_t capacity)
        : capacity_(capacity), data_(capacity), readPos_(0), writePos_(0), resetEpoch_(0) {}

    SPSCRingBuffer(const SPSCRingBuffer&) = delete;
    SPSCRingBuffer& operator=(const SPSCRingBuffer&) = delete;
    SPSCRingBuffer(SPSCRingBuffer&&) = delete;
    SPSCRingBuffer& operator=(SPSCRingBuffer&&) = delete;
    ~SPSCRingBuffer() = default;

    /// 写线程调用：尝试写入恰好 count 个元素，空间不足返回 false
    bool writeExact(const T* src, uint32_t count) noexcept {
        if (count == 0) {
            return true;
        }
        if (src == nullptr || available() < count) {
            return false;
        }
        auto wp = writePos_.load(std::memory_order_relaxed);
        auto firstPart = std::min(count, capacity_ - wp);
        std::copy_n(src, firstPart, data_.data() + wp);
        if (firstPart < count) {
            std::copy_n(src + firstPart, count - firstPart, data_.data());
        }
        writePos_.store((wp + count) % capacity_, std::memory_order_release);
        return true;
    }

    /// 读线程调用：尝试读取恰好 count 个元素，数据不足返回 false
    /// 通过 epoch 检测 reset 并发：若 reset 发生则放弃本次读取，返回 false
    bool readExact(T* dst, uint32_t count) noexcept {
        if (count == 0) {
            return true;
        }
        if (dst == nullptr) {
            return false;
        }
        // 读前记录 epoch，读后再次检查，若 epoch 变化说明 reset 并发发生
        auto epochBefore = resetEpoch_.load(std::memory_order_acquire);
        if (length() < count) {
            return false;
        }
        auto rp = readPos_.load(std::memory_order_relaxed);
        auto firstPart = std::min(count, capacity_ - rp);
        std::copy_n(data_.data() + rp, firstPart, dst);
        if (firstPart < count) {
            std::copy_n(data_.data(), count - firstPart, dst + firstPart);
        }
        // 提交前再次检查 epoch，若 reset 已发生则丢弃本次读取结果
        if (resetEpoch_.load(std::memory_order_acquire) != epochBefore) {
            return false;
        }
        readPos_.store((rp + count) % capacity_, std::memory_order_release);
        return true;
    }

    /// 写线程调用：重置缓冲区（flush/seek 场景）
    /// 先递增 epoch 通知读线程，再重置位置，保证读线程不会提交脏数据
    void reset() noexcept {
        // 递增 epoch，读线程在提交前会检测到 epoch 变化并放弃
        resetEpoch_.fetch_add(1, std::memory_order_acq_rel);
        readPos_.store(0, std::memory_order_release);
        writePos_.store(0, std::memory_order_release);
    }

    [[nodiscard]] uint32_t length() const noexcept {
        auto wp = writePos_.load(std::memory_order_acquire);
        auto rp = readPos_.load(std::memory_order_acquire);
        return (wp >= rp) ? (wp - rp) : (capacity_ - rp + wp);
    }

    [[nodiscard]] uint32_t available() const noexcept {
        return capacity_ - 1 - length(); // 留一个槽位区分满/空
    }

    [[nodiscard]] uint32_t capacity() const noexcept {
        return capacity_;
    }

private:
    uint32_t capacity_;
    std::vector<T> data_;
    alignas(64) std::atomic<uint32_t> readPos_;
    alignas(64) std::atomic<uint32_t> writePos_;
    alignas(64) std::atomic<uint32_t> resetEpoch_;
};

} // namespace slark
