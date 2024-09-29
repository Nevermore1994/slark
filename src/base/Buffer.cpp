//
//  Buffer.cpp
//  slark
//
//  Created by Nevermore.
//

#include "Buffer.hpp"
#include "Util.hpp"
#include <utility>

namespace slark {

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

bool Buffer::checkMinSize(uint64_t size) const noexcept {
    return length() >= size;
}

void Buffer::append(DataPtr ptr) noexcept {
    if (data_) {
        data_->append(std::move(ptr));
    } else {
        data_ = std::move(ptr);
    }
}

std::expected<uint32_t, bool> Buffer::read4ByteBE() noexcept {
    if (length() < 4) {
        return std::unexpected(false);
    }
    auto view = data_->view().substr(readPos_);
    readPos_ += 4;
    return Util::read4ByteBE(view);
}

std::expected<uint32_t, bool> Buffer::read3ByteBE() noexcept {
    if (length() < 3) {
        return std::unexpected(false);
    }
    auto view = data_->view().substr(readPos_);
    readPos_ += 3;
    return Util::read3ByteBE(view);
}

std::expected<uint16_t, bool> Buffer::read2ByteBE() noexcept {
    if (length() < 2) {
        return std::unexpected(false);
    }
    auto view = data_->view().substr(readPos_);
    readPos_ += 2;
    return Util::read2ByteBE(view);
}

std::expected<uint8_t, bool> Buffer::readByteBE() noexcept {
    if (empty()) {
        return std::unexpected(false);
    }
    auto view = data_->view().substr(readPos_);
    readPos_ += 1;
    return Util::readByteBE(view);
}

std::expected<uint32_t, bool> Buffer::read4ByteLE() noexcept {
    if (length() < 4) {
        return std::unexpected(false);
    }
    auto view = data_->view().substr(readPos_);
    readPos_ += 4;
    return Util::read4ByteLE(view);
}

std::expected<uint32_t, bool> Buffer::read3ByteLE() noexcept {
    if (length() < 3) {
        return std::unexpected(false);
    }
    auto view = data_->view().substr(readPos_);
    readPos_ += 3;
    return Util::read3ByteLE(view);
}

std::expected<uint16_t, bool> Buffer::read2ByteLE() noexcept {
    if (length() < 2) {
        return std::unexpected(false);
    }
    auto view = data_->view().substr(readPos_);
    readPos_ += 2;
    return Util::read2ByteLE(view);
}

std::expected<uint8_t, bool> Buffer::readByteLE() noexcept {
    if (empty()) {
        return std::unexpected(false);
    }
    auto view = data_->view().substr(readPos_);
    readPos_ += 1;
    return Util::readByteLE(view);
}

DataPtr Buffer::readData(uint64_t size) noexcept {
    if (length() < size) {
        return nullptr;
    }
    auto ptr = data_->copy(readPos_, static_cast<int64_t>(size));
    readPos_ += size;
    return ptr;
}

void Buffer::shrink() noexcept {
    auto p = data_->copy(readPos_);
    data_.reset();
    data_ = std::move(p);
    readPos_ = 0;
}

void Buffer::reset() noexcept {
    data_.reset();
    readPos_ = 0;
}

}
