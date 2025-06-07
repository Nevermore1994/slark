//
//  Buffer.hpp
//  slark
//
//  Created by Nevermore.
//

#pragma once
#include <expected>
#include <shared_mutex>
#include "Data.hpp"
#include "NonCopyable.h"

namespace slark {

class Buffer: public NonCopyable {
public:
    Buffer() = default;
    
    Buffer(uint64_t size);
    
    bool empty() const noexcept;

    bool require(uint64_t) const noexcept;
    
    uint64_t length() const noexcept;
    
    uint64_t totalLength() const noexcept;
    
    bool append(DataPtr) noexcept;
    
    bool append(uint64_t offset, DataPtr) noexcept;
    
    DataView shotView() const noexcept;
    
    DataView view() const noexcept;
    
    bool read8ByteLE(uint64_t& value) noexcept;
    
    bool read4ByteLE(uint32_t& value) noexcept;

    bool read3ByteLE(uint32_t& value) noexcept;

    bool read2ByteLE(uint16_t& value) noexcept;

    bool read8ByteBE(uint64_t& value) noexcept;
    
    bool read4ByteBE(uint32_t& value) noexcept;

    bool read3ByteBE(uint32_t& value) noexcept;

    bool read2ByteBE(uint16_t& value) noexcept;

    bool readByte(uint8_t& value) noexcept;
    
    bool readBE(uint32_t size, uint32_t& value) noexcept;

    bool readLE(uint32_t size, uint32_t& value) noexcept;
    
    DataPtr readData(uint64_t) noexcept;
    
    bool readString(uint64_t size, std::string& str) noexcept;
    
    bool readLine(DataView& line) noexcept;
    
    bool skipTo(int64_t pos) noexcept;
    
    bool skip(int64_t) noexcept;
    
    void shrink() noexcept;
    
    void reset() noexcept;
    
    [[nodiscard]] uint64_t pos() const noexcept {
        return readPos_ + offset_;
    }
    
    [[nodiscard]] uint64_t readPos() const noexcept {
        return readPos_;
    }
    
    void resetReadPos() noexcept {
        readPos_ = 0;
    }
    
    uint64_t totalSize() const noexcept {
        return totalSize_;
    }
    
    void setOffset(uint64_t offset) noexcept {
        offset_ = offset;
        isUpdatedOffset = true;
    }
    
    uint64_t offset() const noexcept {
        return offset_;
    }
    
    uint64_t end() const noexcept {
        return offset() + totalLength();
    }
private:
    bool isUpdatedOffset = false;
    DataPtr data_ = nullptr;
    uint64_t readPos_ = 0;
    uint64_t offset_ = 0;
    uint64_t totalSize_ = 0;
};


class BitReadView{
public:
    explicit BitReadView(Buffer& buffer)
        : buffer_(buffer){

    }

    uint8_t readBit() {
        uint8_t value = 0;
        if (bitPos == 0) {
            if (!buffer_.readByte(bitBuffer)) {
                return false;
            }
            bitPos = 8;
        }
        value = (bitBuffer >> (bitPos - 1)) & 0x01;
        bitPos--;
        return value;
    }

    ///Please note that before reading, you should ensure that the readable range is not exceeded,
    ///otherwise the problem of discarding intermediate data will occur!
    uint32_t readBits(uint32_t n) {
        uint32_t val = 0;
        if (!buffer_.require(n / 8)) {
            return false;
        }
        for (uint32_t i = 0; i < n; ++i) {
            val = (val << 1) | readBit();
        }
        return val;
    }
private:
    Buffer& buffer_;
    uint8_t bitBuffer = 0;
    int8_t bitPos = 0;
};

}
