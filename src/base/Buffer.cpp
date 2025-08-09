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
    : totalSize_(size) {
    
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

uint64_t Buffer::totalLength() const noexcept {
    if (empty()) {
        return 0;
    }
    return data_->length;
}

bool Buffer::require(uint64_t size) const noexcept {
    return length() >= size;
}

bool Buffer::append(DataPtr ptr) noexcept {
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

bool Buffer::append(uint64_t offset, DataPtr ptr) noexcept {
    if (!ptr) {
        return false;
    }
    if (isUpdatedOffset && offset_ != offset) {
        return false; //discard expired data
    }
    if (offset_ > offset) {
        LogI("reset buffer:offset {}, now offset:{}, discard:{}", offset, offset_, length());
        data_.reset();
        offset_ = offset;
    }
    append(std::move(ptr));
    isUpdatedOffset = false;
    return true;
}

DataView Buffer::view() const noexcept {
    if (data_) {
        return data_->view();
    }
    return {};
}

DataView Buffer::shotView() const noexcept {
    if (!data_) {
        return {};
    }
    return data_->view().substr(static_cast<size_t>(readPos_));
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
    if (view.length() < size) {
        return false;
    }
    view = view.substr(0, static_cast<size_t>(size));
    readPos_ += size;
    return Util::readBE(view, size, value);
}

bool Buffer::readLE(uint32_t size, uint32_t& value) noexcept {
    if (!require(size) || size == 0) {
        return false;
    }
    auto view = data_->view().substr(static_cast<size_t>(readPos_));
    if (view.length() < size) {
        return false;
    }
    view = view.substr(0, static_cast<size_t>(size));
    readPos_ += size;
    return Util::readLE(view, size, value);
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
    str = view.str();
    return true;
}

bool Buffer::readLine(DataView& line) noexcept {
    auto view = data_->view().substr(static_cast<size_t>(readPos_));
    auto pos = view.find("\n");
    if (pos == std::string_view::npos) {
        return false;
    }
    line = view.substr(0, pos);
    readPos_ += pos + 1; //remove '\n'
    return true;
}

//If this function is called, the previous view cannot be used anymore
void Buffer::shrink() noexcept {
    if (readPos_ == 0) {
        return;
    }
    auto p = data_->copy(readPos_);
    data_.reset();
    data_ = std::move(p);
    offset_ += readPos_;
    readPos_ = 0;
}

DataPtr Buffer::detachData() noexcept {
    auto data = std::move(data_);
    reset();
    return data;
}

void Buffer::reset() noexcept {
    data_.reset();
    readPos_ = 0;
    offset_ = 0;
}

}
