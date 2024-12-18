//
//  Buffer.cpp
//  slark
//
//  Created by Nevermore.
//

#include <utility>
#include "Buffer.hpp"
#include "Util.hpp"
#include "Log.hpp"

namespace slark {

Buffer::Buffer(uint64_t size)
    :totalSize_(size) {
    
}

bool Buffer::empty() const noexcept {
    if (!data_) {
        return true;
    }
    return data_->length <= readPos_;
}

uint64_t Buffer::length() const noexcept {
    if (empty()) {
        return 0;
    }
    return data_->length - readPos_;
}

bool Buffer::require(uint64_t size) const noexcept {
    return length() >= size;
}

bool Buffer::append(uint64_t offset, DataPtr ptr) noexcept {
    if (!ptr) {
        return false;
    }
    if (isUpdatedOffset && offset_ != offset) {
        return false; //discard expired data
    }
    if (offset_ > offset) {
        LogI("reset buffer:offset {}, now offset:{}", offset, offset_);
        data_.reset();
        offset_ = offset;
    }
    if (data_) {
        data_->append(std::move(ptr));
    } else {
        data_ = std::move(ptr);
    }
    isUpdatedOffset = false;
    return true;
}

std::string_view Buffer::view() const noexcept {
    if (data_) {
        return data_->view();
    }
    return {};
}

bool Buffer::skipTo(int64_t pos) noexcept {
    auto p = pos - static_cast<int64_t>(offset_);
    if (0 <= p && p <= static_cast<int64_t>(data_->length)) {
        readPos_ = static_cast<uint64_t>(p);
        return true;
    }
    return false;
}

bool Buffer::skip(int64_t skipOffset) noexcept {
    auto pos = static_cast<int64_t>(readPos_) + skipOffset;
    if (0 <= pos && pos <= static_cast<int64_t>(data_->length)) {
        readPos_ = static_cast<uint64_t>(pos);
        return true;
    }
    return false;
}

bool Buffer::read8ByteBE(uint64_t& value) noexcept {
    if (!require(8)) {
        return false;
    }
    auto view = data_->view().substr(static_cast<size_t>(readPos_));
    readPos_ += 8;
    return Util::read8ByteBE(view, value);
}

bool Buffer::read4ByteBE(uint32_t& value) noexcept {
    if (!require(4)) {
        return false;
    }
    auto view = data_->view().substr(static_cast<size_t>(readPos_));
    readPos_ += 4;
    return Util::read4ByteBE(view, value);
}

bool Buffer::read3ByteBE(uint32_t& value) noexcept {
    if (!require(3)) {
        return false;
    }
    auto view = data_->view().substr(static_cast<size_t>(readPos_));
    readPos_ += 3;
    return Util::read3ByteBE(view, value);
}

bool Buffer::read2ByteBE(uint16_t& value) noexcept {
    if (!require(2)) {
        return false;
    }
    auto view = data_->view().substr(static_cast<size_t>(readPos_));
    readPos_ += 2;
    return Util::read2ByteBE(view, value);
}

bool Buffer::readBE(uint32_t size, uint32_t& value) noexcept {
    if (!require(size) || size == 0) {
        return false;
    }
    auto view = data_->view().substr(static_cast<size_t>(readPos_));
    size_t mx = size - 1;
    auto func = [&](size_t pos) {
        return static_cast<uint32_t>(static_cast<uint8_t>(view[pos]) << ((mx - pos) * 8));
    };
    for(size_t i = 0; i < mx; i++) {
        value |= func(i);
    }
    return true;
}

bool Buffer::readByte(uint8_t& value) noexcept {
    if (empty()) {
        return false;
    }
    auto view = data_->view().substr(static_cast<size_t>(readPos_));
    readPos_ += 1;
    return Util::readByte(view, value);
}

bool Buffer::read8ByteLE(uint64_t& value) noexcept {
    if (!require(8)) {
        return false;
    }
    auto view = data_->view().substr(static_cast<size_t>(readPos_));
    readPos_ += 8;
    return Util::read8ByteLE(view, value);
}

bool Buffer::read4ByteLE(uint32_t& value) noexcept {
    if (!require(4)) {
        return false;
    }
    auto view = data_->view().substr(static_cast<size_t>(readPos_));
    readPos_ += 4;
    return Util::read4ByteLE(view, value);
}

bool Buffer::read3ByteLE(uint32_t& value) noexcept {
    if (!require(3)) {
        return false;
    }
    auto view = data_->view().substr(static_cast<size_t>(readPos_));
    readPos_ += 3;
    return Util::read3ByteLE(view, value);
}

bool Buffer::read2ByteLE(uint16_t& value) noexcept {
    if (!require(2)) {
        return false;
    }
    auto view = data_->view().substr(static_cast<size_t>(readPos_));
    readPos_ += 2;
    return Util::read2ByteLE(view, value);
}

DataPtr Buffer::readData(uint64_t size) noexcept {
    if (!require(size)) {
        return nullptr;
    }
    auto ptr = data_->copy(readPos_, static_cast<int64_t>(size));
    readPos_ += size;
    return ptr;
}

bool Buffer::readString(uint64_t size, std::string& str) noexcept {
    if (!require(size)) {
        return false;
    }
    
    auto view = data_->view().substr(static_cast<size_t>(readPos_), static_cast<size_t>(size));
    readPos_ += size;
    str = std::string(view);
    return true;
}

void Buffer::shrink() noexcept {
    auto p = data_->copy(readPos_);
    data_.reset();
    data_ = std::move(p);
    offset_ += readPos_;
    readPos_ = 0;
}

void Buffer::reset() noexcept {
    data_.reset();
    readPos_ = 0;
    offset_ = 0;
}

}
