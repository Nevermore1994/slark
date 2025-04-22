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

namespace slark {

class NormalSync {
public:
    using CounterType = uint32_t;

    static void store(CounterType& dest, uint32_t value) noexcept {
        dest = value;
    }

    static uint32_t load(const CounterType& src) noexcept {
        return src;
    }

    static void increment(CounterType& counter, uint32_t value) noexcept {
        counter += value;
    }

    static void decrement(CounterType& counter, uint32_t value) noexcept {
        counter -= value;
    }
};

class LockFreeSync {
public:
    using CounterType = std::atomic<uint32_t>;

    static void store(CounterType& dest, uint32_t value) noexcept {
        dest.store(value, std::memory_order_release);
    }

    static uint32_t load(const CounterType& src) noexcept {
        return src.load(std::memory_order_acquire);
    }

    static void increment(CounterType& counter, uint32_t value) noexcept {
        counter.fetch_add(value, std::memory_order_acq_rel);
    }

    static void decrement(CounterType& counter, uint32_t value) noexcept {
        counter.fetch_sub(value, std::memory_order_acq_rel);
    }
};

constexpr uint32_t RingBufferLength = 1024 * 4;

template<typename T, uint32_t Capacity = RingBufferLength, typename SyncStrategy = NormalSync>
class RingBufferImpl {
public:
    using Counter = typename SyncStrategy::CounterType;

    RingBufferImpl()
            : data_(std::make_unique<std::array<T, Capacity>>())
            , readPos_(0)
            , writePos_(0)
            , size_(0) {}
public:
    /**
     * @brief Appends data to the ring buffer.
     * @param data Pointer to the data to be appended.
     * @param size Number of elements to append.
     * @return The actual number of elements written.
     */
    uint32_t write(const T* data, uint32_t size) noexcept {
        if (data == nullptr) {
            return 0;
        }
        if (size == 0 || isFull()) {
            return 0;
        }
        size = std::min(size, tail());

        auto writePosition = getWritePos();
        auto firstPart = std::min(size, Capacity - writePosition);
        std::copy(data, data + firstPart, data_->data() + writePosition);

        if (size > firstPart) {
            auto secondPart = size - firstPart;
            std::copy(data + firstPart, data + size, data_->data());
            setWritePos(secondPart);
        } else {
            setWritePos((writePosition + size) % Capacity);
        }

        incrementSize(size);
        return size;
    }

    /**
     * @brief Reads data from the ring buffer.
     * @param data Pointer to the buffer where the data will be stored.
     * @param size Number of elements to read.
     * @return The actual number of elements read.
     */
    uint32_t read(T* data, uint32_t size) noexcept {
        if (data == nullptr) {
            return 0;
        }
        if (size == 0 || isEmpty()) {
            return 0;
        }

        size = std::min(size, length());
        auto readPosition = getReadPos();

        auto firstPart = std::min(size, Capacity - readPosition);
        std::copy(data_->data() + readPosition, data_->data() + readPosition + firstPart, data);

        if (size > firstPart) {
            auto secondPart = size - firstPart;
            std::copy(data_->data(), data_->data() + secondPart, data + firstPart);
            setReadPos(secondPart);
        } else {
            setReadPos((readPosition + size) % Capacity);
        }

        decrementSize(size);
        return size;
    }

    /**
     * @brief Writes data to the ring buffer only if there is enough space.
     * @param data Pointer to the data to be written.
     * @param size Number of elements to write.
     * @return True if the data was successfully written, false otherwise.
     */
    bool writeExact(const T* data, uint32_t size) noexcept {
        if (data == nullptr) {
            return false;
        }
        if (size == 0) {
            return true;
        }
        if (tail() < size) {
            return false;
        }
        auto writeSize = write(data, size);
        assert(writeSize == size);
        return writeSize == size;
    }

    /**
    * @brief Reads data from the ring buffer only if there is enough data available.
    * @param data Pointer to the buffer where the data will be stored.
    * @param size Number of elements to read.
    * @return True if the data was successfully read, false otherwise.
    */
    bool readExact(T* data, uint32_t size) noexcept {
        if (data == nullptr) {
            return false;
        }
        if (size == 0) {
            return true;
        }
        if (length() < size) {
            return false;
        }
        auto readSize = read(data, size);
        assert(readSize == size);
        return readSize == size;
    }

    /**
     * @brief Resets the ring buffer, clearing all data.
     * Note: This does release the memory and the buffer state.
     */
    void reset() noexcept {
        setSize(0);
        setReadPos(0);
        setWritePos(0);
    }

    /**
     * @brief Returns the remaining capacity of the ring buffer.
     * @return The number of elements that can still be written to the buffer.
     */
    [[nodiscard]] uint32_t tail() const noexcept {
        return Capacity - getSize();
    }

    /**
     * @brief Returns the total capacity of the ring buffer.
     * @return The maximum number of elements the buffer can hold.
     */
    [[nodiscard]] uint32_t capacity() const noexcept {
        return Capacity;
    }

    /**
     * @brief Returns the current size of the ring buffer.
     * @return The number of elements currently stored in the buffer.
     */
    [[nodiscard]] uint32_t length() const noexcept {
        return getSize();
    }

    /**
     * @brief Checks if the ring buffer is empty.
     * @return True if the buffer is empty, false otherwise.
     */
    [[nodiscard]] bool isEmpty() const noexcept {
        return getSize() == 0;
    }

    /**
     * @brief Checks if the ring buffer is full.
     * @return True if the buffer is full, false otherwise.
     */
    [[nodiscard]] bool isFull() const noexcept {
        return getSize() == Capacity;
    }

    [[nodiscard]] bool require(uint32_t size) const noexcept {
        return tail() >= size;
    }
private:
    [[nodiscard]] uint32_t getSize() const noexcept {
        return SyncStrategy::load(size_);
    }

    [[nodiscard]] uint32_t getReadPos() const noexcept {
        return SyncStrategy::load(readPos_);
    }

    [[nodiscard]] uint32_t getWritePos() const noexcept {
        return SyncStrategy::load(writePos_);
    }

    void setSize(uint32_t size) noexcept {
        SyncStrategy::store(size_, size);
    }

    void setReadPos(uint32_t pos) noexcept {
        SyncStrategy::store(readPos_, pos);
    }

    void setWritePos(uint32_t pos) noexcept {
        SyncStrategy::store(writePos_, pos);
    }

    void incrementSize(uint32_t delta) noexcept {
        SyncStrategy::increment(size_, delta);
    }

    void decrementSize(uint32_t delta) noexcept {
        SyncStrategy::decrement(size_, delta);
    }

private:
    std::unique_ptr<std::array<T, Capacity>> data_;
    Counter readPos_;
    Counter writePos_;
    Counter size_;
};

template<typename T, uint32_t Capacity = RingBufferLength>
using RingBuffer = RingBufferImpl<T, Capacity, NormalSync>;

template<typename T, uint32_t Capacity = RingBufferLength>
using SPSCRingBuffer = RingBufferImpl<T, Capacity, LockFreeSync>;

} // namespace slark