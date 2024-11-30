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
    Buffer(uint64_t size); //fix me
    bool empty() const noexcept;

    bool require(uint64_t) const noexcept;
    
    uint64_t length() const noexcept;
    
    bool append(uint64_t offset, DataPtr) noexcept;
    
    std::string_view view() const noexcept;
    
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
    
    DataPtr readData(uint64_t) noexcept;
    
    bool readString(uint64_t size, std::string& str) noexcept;
    
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
    
private:
    bool isUpdatedOffset = false;
    DataPtr data_ = nullptr;
    uint64_t readPos_ = 0;
    uint64_t offset_ = 0;
    uint64_t totalSize_ = 0;
};

}
