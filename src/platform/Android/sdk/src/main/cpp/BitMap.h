//
// Created by Nevermore- on 2025/5/6.
//
#pragma once

#include "Data.hpp"

namespace slark {

enum class BitMapFormat : uint8_t {
    RGB,
    RGBA,
    ARGB,
    BGRA,
    GRAY,
    APLHA,
};

class BitMap {
public:
    BitMap(
        BitMapFormat format,
        uint32_t width,
        uint32_t height,
        DataPtr data
    )
        : format_(format),
          width_(width),
          height_(height),
          data_(std::move(data)) {
    }

    ~BitMap() = default;

    BitMap(const BitMap &) = delete;

    BitMap &operator=(const BitMap &) = delete;

    BitMap(BitMap &&rhs) noexcept
        : format_(rhs.format_),
          width_(rhs.width_),
          height_(rhs.height_),
          data_(std::move(rhs.data_)) {

    }

    BitMap &operator=(BitMap &&rhs) noexcept {
        if (this != &rhs) {
            format_ = rhs.format_;
            width_ = rhs.width_;
            height_ = rhs.height_;
            data_ = std::move(rhs.data_);
        }
        return *this;
    }

    BitMapFormat format() const noexcept {
        return format_;
    }

    uint32_t width() const noexcept {
        return width_;
    }

    uint32_t height() const noexcept {
        return height_;
    }

    uint8_t *data() const noexcept {
        if (data_) {
            return data_->rawData;
        }
        return nullptr;
    }

    uint8_t bytePerPixel() const noexcept {
        if (format_ == BitMapFormat::RGB) {
            return 3;
        } else if (format_ == BitMapFormat::GRAY || format_ == BitMapFormat::APLHA) {
            return 1;
        }
        return 4;
    }

    static uint8_t bytePerPixel(BitMapFormat format) noexcept {
        if (format == BitMapFormat::RGB) {
            return 3;
        } else if (format == BitMapFormat::GRAY || format == BitMapFormat::APLHA) {
            return 1;
        }
        return 4;
    }

private:
    BitMapFormat format_;
    uint32_t width_ = 0;
    uint32_t height_ = 0;
    DataPtr data_;
};

using BitMapPtr = std::unique_ptr<BitMap>;
using BitMapRefPtr = std::shared_ptr<BitMap>;
} // slark

