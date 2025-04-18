//
// Created by Nevermore on 2023/11/20.
// slark Buffer
// Copyright (c) 2023 Nevermore All rights reserved.
//
#pragma once

#include <cstdint>
#include <array>
#include <algorithm>
#include <memory>
#include <cassert>
#include "NonCopyable.h"

namespace slark {

constexpr uint32_t RingBufferLength = 1024 * 1024 * 16;

template<typename T, uint32_t Capacity = RingBufferLength>
class RingBuffer : public NonCopyable {
public:
    RingBuffer()
        : data_(std::make_unique<std::array<T, Capacity>>()) {}
    ~RingBuffer() override = default;
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
        size = std::min(size, tail()); // Adjust size to available space

        auto firstPart = std::min(size, Capacity - writePos_);
        std::copy(data, data + firstPart, data_->data() + writePos_);
        writePos_ = (writePos_ + firstPart) % Capacity;
        if (size > firstPart) {
            auto secondPart = size - firstPart;
            std::copy(data + firstPart, data + firstPart + secondPart, data_->data());
            writePos_ = secondPart;
        }
        size_ += size;
        return size;  // Return actual written size
    }

    /**
     * @brief Reads data from the ring buffer.
     * @param data Pointer to the buffer where the data will be stored.
     * @param size Number of elements to read.
     * @return The actual number of elements read.
     */
    uint32_t read(T* data, uint32_t size) noexcept {
        assert(data != nullptr);
        if (size == 0 || isEmpty()) {
            return 0;
        }

        size = std::min(size, length()); // Adjust size to available data

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
        data_.reset();
        writePos_ = 0;
        readPos_ = 0;
        size_ = 0;
    }

    /**
     * @brief Returns the remaining capacity of the ring buffer.
     * @return The number of elements that can still be written to the buffer.
     */
    [[nodiscard]] uint32_t tail() const noexcept {
        return Capacity - size_;
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
    [[nodiscard]] inline uint32_t length() const noexcept {
        return size_;
    }

    /**
     * @brief Checks if the ring buffer is empty.
     * @return True if the buffer is empty, false otherwise.
     */
    [[nodiscard]] bool isEmpty() const noexcept {
        return size_ == 0;
    }

    /**
     * @brief Checks if the ring buffer is full.
     * @return True if the buffer is full, false otherwise.
     */
    [[nodiscard]] bool isFull() const noexcept {
        return size_ == Capacity;
    }

private:
    std::unique_ptr<std::array<T, Capacity>> data_;
    uint32_t readPos_ = 0;
    uint32_t writePos_ = 0;
    uint32_t size_ = 0;
};

}



