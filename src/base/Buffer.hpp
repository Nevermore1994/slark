//
//  Buffer.hpp
//  slark
//
//  Created by Nevermore.

#pragma once

#include "Data.h"
#include "DataView.hpp"
#include "NonCopyable.h"

#include <expected>
#include <string>

namespace slark {

class Buffer : public NonCopyable {
public:
    Buffer() = default;

    explicit Buffer(uint64_t size) : totalSize_(size) {}

    [[nodiscard]] bool empty() const noexcept {
        return !data_ || data_->length() <= readPos_;
    }

    [[nodiscard]] bool require(uint64_t size) const noexcept {
        return length() >= size;
    }

    [[nodiscard]] uint64_t length() const noexcept {
        return empty() ? 0 : data_->length() - readPos_;
    }

    [[nodiscard]] uint64_t totalLength() const noexcept {
        return empty() ? 0 : data_->length();
    }

    bool append(DataPtr ptr) noexcept {
        if (!ptr) {
            return false;
        }
        if (data_) {
            data_->append(std::move(ptr));
        } else {
            data_ = std::move(ptr);
        }
        return true;
    }

    bool append(uint64_t offset, DataPtr ptr) noexcept {
        if (!ptr || (isUpdatedOffset_ && offset_ != offset)) {
            return false;
        }
        if (offset_ > offset) {
            data_.reset();
            offset_ = offset;
        }
        append(std::move(ptr));
        isUpdatedOffset_ = false;
        return true;
    }

    [[nodiscard]] DataView view() const noexcept {
        return data_ ? DataView(data_->view()) : DataView{};
    }

    [[nodiscard]] DataView shotView() const noexcept {
        return data_ ? DataView(data_->view(readPos_)) : DataView{};
    }

    bool readByte(uint8_t& value) noexcept {
        return readNumberLE(value, 1);
    }

    bool read2ByteLE(uint16_t& value) noexcept {
        return readNumberLE(value, 2);
    }

    bool read3ByteLE(uint32_t& value) noexcept {
        return readNumberLE(value, 3);
    }

    bool read4ByteLE(uint32_t& value) noexcept {
        return readNumberLE(value, 4);
    }

    bool read8ByteLE(uint64_t& value) noexcept {
        return readNumberLE(value, 8);
    }

    bool read2ByteBE(uint16_t& value) noexcept {
        return readNumberBE(value, 2);
    }

    bool read3ByteBE(uint32_t& value) noexcept {
        return readNumberBE(value, 3);
    }

    bool read4ByteBE(uint32_t& value) noexcept {
        return readNumberBE(value, 4);
    }

    bool read8ByteBE(uint64_t& value) noexcept {
        return readNumberBE(value, 8);
    }

    bool readLE(uint32_t size, uint32_t& value) noexcept {
        return readNumberLE(value, size);
    }

    bool readBE(uint32_t size, uint32_t& value) noexcept {
        return readNumberBE(value, size);
    }

    template <typename T>
    bool readNumberLE(T& value, uint32_t bytes = sizeof(T)) noexcept {
        return readNumber(value, bytes, false);
    }

    template <typename T>
    bool readNumberBE(T& value, uint32_t bytes = sizeof(T)) noexcept {
        return readNumber(value, bytes, true);
    }

    DataPtr readData(uint64_t size) noexcept {
        if (!require(size)) {
            return nullptr;
        }
        auto result = data_->safeCopy(readPos_, static_cast<int64_t>(size));
        if (!result) {
            return nullptr;
        }
        readPos_ += size;
        return std::move(*result);
    }

    bool readString(uint64_t size, std::string& str) noexcept {
        if (!require(size)) {
            return false;
        }
        auto view = data_->view(readPos_, size);
        readPos_ += size;
        str = std::string(view.begin(), view.end());
        return true;
    }

    bool readLine(DataView& line) noexcept {
        auto span = data_->view(readPos_);
        auto it = std::find(span.begin(), span.end(), static_cast<Byte>('\n'));
        if (it == span.end()) {
            return false;
        }
        auto pos = static_cast<uint64_t>(std::distance(span.begin(), it));
        line = DataView(span.first(pos));
        readPos_ += pos + 1;
        return true;
    }

    bool skipTo(int64_t pos) noexcept {
        auto p = pos - static_cast<int64_t>(offset_);
        if (p < 0 || !data_ || p > static_cast<int64_t>(data_->length())) {
            return false;
        }
        readPos_ = static_cast<uint64_t>(p);
        return true;
    }

    bool skip(int64_t offset) noexcept {
        auto pos = static_cast<int64_t>(readPos_) + offset;
        if (pos < 0 || !data_ || pos > static_cast<int64_t>(data_->length())) {
            return false;
        }
        readPos_ = static_cast<uint64_t>(pos);
        return true;
    }

    void shrink() noexcept {
        if (readPos_ == 0 || !data_) {
            return;
        }
        auto p = data_->copy(readPos_);
        data_.reset();
        data_ = std::move(p);
        offset_ += readPos_;
        readPos_ = 0;
    }

    void reset() noexcept {
        data_.reset();
        readPos_ = 0;
        offset_ = 0;
        isUpdatedOffset_ = false;
    }

    DataPtr detachData() noexcept {
        auto data = std::move(data_);
        reset();
        return data;
    }

    [[nodiscard]] uint64_t pos() const noexcept {
        return readPos_ + offset_;
    }

    [[nodiscard]] uint64_t readPos() const noexcept {
        return readPos_;
    }

    void resetReadPos() noexcept {
        readPos_ = 0;
    }

    [[nodiscard]] uint64_t totalSize() const noexcept {
        return totalSize_;
    }

    void setOffset(uint64_t offset) noexcept {
        offset_ = offset;
        isUpdatedOffset_ = true;
    }

    [[nodiscard]] uint64_t offset() const noexcept {
        return offset_;
    }

    [[nodiscard]] uint64_t end() const noexcept {
        return offset() + totalLength();
    }

private:
    template <typename T>
    bool readNumber(T& value, uint32_t bytes, bool bigEndian) noexcept {
        static_assert(std::is_unsigned_v<T>);
        if (bytes > sizeof(T) || !require(bytes)) {
            return false;
        }
        auto view = shotView();
        bool ok = bigEndian ? view.readNumberBE(value, bytes) : view.readNumberLE(value, bytes);
        if (ok) {
            readPos_ += bytes;
        }
        return ok;
    }

private:
    DataPtr data_ = nullptr;
    uint64_t readPos_ = 0;
    uint64_t offset_ = 0;
    uint64_t totalSize_ = 0;
    bool isUpdatedOffset_ = false;
};

class BitReadView {
public:
    explicit BitReadView(Buffer& buffer) : buffer_(buffer) {}

    uint8_t readBit() noexcept {
        if (bitPos_ == 0) {
            uint8_t byte = 0;
            if (!buffer_.readByte(byte)) {
                return 0;
            }
            bitBuffer_ = byte;
            bitPos_ = 8;
        }
        uint32_t value =
            static_cast<uint32_t>(static_cast<uint8_t>((bitBuffer_ >> (bitPos_ - 1)) & 0x01));
        bitPos_--;
        return static_cast<uint8_t>(value);
    }

    uint32_t readBits(uint32_t n) noexcept {
        if (!buffer_.require((n + bitPos_) / 8)) {
            return 0;
        }
        uint32_t value = 0;
        for (uint32_t i = 0; i < n; ++i) {
            value = (value << 1) | static_cast<uint32_t>(readBit());
        }
        return value;
    }

private:
    Buffer& buffer_;
    uint8_t bitBuffer_ = 0;
    uint8_t bitPos_ = 0;
};

} // namespace slark
