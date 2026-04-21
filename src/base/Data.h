//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#pragma once

#include "ByteUtils.hpp"
#include "DataView.hpp"
#include "Error.h"
#include "Types.h"

#include <algorithm>
#include <compare>
#include <cstdint>
#include <format>
#include <functional>
#include <memory>
#include <span>
#include <string_view>
#include <vector>

namespace slark {

class Data;
class DataView;

using DataPtr = std::unique_ptr<Data>;
using DataRefPtr = std::shared_ptr<Data>;
using DataSpan = std::span<const uint8_t>;

// 二进制数据管理类
class Data {
public:
    Data() = default;

    Data(uint64_t size, const Byte* data) {
        if (size > 0 && data != nullptr && allocate(size)) {
            std::copy_n(data, size, rawData_.get());
            length_ = size;
        }
    }

    explicit Data(uint64_t size) {
        if (size > 0 && allocate(size)) {
            std::fill_n(rawData_.get(), size, Byte{0});
            length_ = size;
        }
    }

    explicit Data(uint64_t size, const std::function<uint64_t(Byte*)>& func) {
        if (size > 0 && allocate(size) && func) {
            length_ = func(rawData_.get());
        }
    }

    explicit Data(ConstByteSpan span) : Data(static_cast<uint64_t>(span.size()), span.data()) {}

    explicit Data(DataView dataView)
        : Data(static_cast<uint64_t>(dataView.size()), dataView.data()) {}

    explicit Data(std::string_view str)
        : Data(static_cast<uint64_t>(str.size()), reinterpret_cast<const Byte*>(str.data())) {}

    ~Data() = default;

    Data(const Data& other) {
        if (!other.empty() && allocate(other.capacity_)) {
            copyFrom(other);
        }
    }

    Data(Data&& other) noexcept
        : capacity_(other.capacity_), length_(other.length_), rawData_(std::move(other.rawData_)) {
        other.reset();
    }

    Data& operator=(const Data& other) {
        if (this != &other) {
            reset();
            if (!other.empty() && allocate(other.capacity_)) {
                copyFrom(other);
            }
        }
        return *this;
    }

    Data& operator=(Data&& other) noexcept {
        if (this != &other) {
            reset();
            capacity_ = other.capacity_;
            length_ = other.length_;
            rawData_ = std::move(other.rawData_);
            other.reset();
        }
        return *this;
    }

    [[nodiscard]] static DataPtr fromString(std::string_view str) {
        return std::make_unique<Data>(str);
    }

    [[nodiscard]] static DataPtr fromSpan(ConstByteSpan span) {
        return std::make_unique<Data>(span);
    }

    template <ByteLike T>
    [[nodiscard]] static DataPtr fromVector(const std::vector<T>& vec) {
        return std::make_unique<Data>(static_cast<uint64_t>(vec.size()),
                                      reinterpret_cast<const Byte*>(vec.data()));
    }

    [[nodiscard]] static DataPtr concat(std::initializer_list<ConstByteSpan> views) {
        uint64_t totalSize = 0;
        for (const auto& view : views) {
            totalSize += static_cast<uint64_t>(view.size());
        }
        auto result = std::make_unique<Data>(totalSize);
        if (totalSize > 0 && result->rawData_) {
            uint64_t offset = 0;
            for (const auto& view : views) {
                if (!view.empty()) {
                    std::copy_n(view.data(), view.size(), result->rawData_.get() + offset);
                    offset += static_cast<uint64_t>(view.size());
                }
            }
            result->length_ = totalSize;
        }
        return result;
    }

    [[nodiscard]] static DataPtr create(uint64_t size) {
        return std::make_unique<Data>(size);
    }

    [[nodiscard]] Expected<DataPtr> safeCopy(uint64_t pos = 0, int64_t len = -1) const noexcept {
        if (empty() || len == 0 || pos >= length_) {
            return std::make_unique<Data>();
        }

        auto size = std::min(len < 0 ? length_ - pos : static_cast<uint64_t>(len), length_ - pos);
        auto result = std::make_unique<Data>();
        if (size > 0) {
            result->rawData_ = std::unique_ptr<Byte[]>(new (std::nothrow) Byte[size]);
            if (!result->rawData_) {
                return makeUnexpected<DataPtr>(Error::OutOfMemory);
            }
            result->capacity_ = size;
            result->length_ = size;
            std::copy_n(rawData_.get() + pos, size, result->rawData_.get());
        }
        return result;
    }

    [[nodiscard]] Expected<void> safeAppend(ConstByteSpan span) noexcept {
        if (span.empty()) {
            return {};
        }
        if (!ensureCapacity(length_ + static_cast<uint64_t>(span.size()))) {
            return makeUnexpected<void>(Error::OutOfMemory);
        }
        std::copy_n(span.data(), span.size(), rawData_.get() + length_);
        length_ += static_cast<uint64_t>(span.size());
        return {};
    }

    [[nodiscard]] Expected<void> safeAppend(std::string_view str) noexcept {
        return safeAppend(toByteSpan(str));
    }

    [[nodiscard]] std::optional<Byte> safeAt(uint64_t pos) const noexcept {
        if (pos >= length_) {
            return std::nullopt;
        }
        return rawData_[pos];
    }

    [[nodiscard]] DataPtr copy(uint64_t pos = 0, int64_t len = -1) const noexcept {
        auto result = safeCopy(pos, len);
        return result.has_value() ? std::move(*result) : std::make_unique<Data>();
    }

    void reset() noexcept {
        rawData_.reset();
        length_ = 0;
        capacity_ = 0;
    }

    void resetData() noexcept {
        if (rawData_) {
            std::fill_n(rawData_.get(), capacity_, Byte{0});
        }
        length_ = 0;
    }

    void setSize(uint64_t size) noexcept {
        length_ = std::min(size, capacity_);
    }

    void resize(uint64_t size) noexcept {
        if (size == 0) {
            reset();
        } else if (size != capacity_) {
            auto newBuffer = std::unique_ptr<Byte[]>(new (std::nothrow) Byte[size]);
            if (newBuffer) {
                auto contentLength = std::min(size, length_);
                if (rawData_) {
                    std::copy_n(rawData_.get(), contentLength, newBuffer.get());
                }
                rawData_ = std::move(newBuffer);
                length_ = contentLength;
                capacity_ = size;
            }
        }
    }

    DataPtr detachData() noexcept {
        auto res = std::make_unique<Data>();
        res->length_ = length_;
        res->capacity_ = capacity_;
        res->rawData_ = std::move(rawData_);
        reset();
        return res;
    }

    Data& append(ConstByteSpan view) noexcept {
        return appendData(view.data(), view.size());
    }

    Data& append(DataView dataView) noexcept {
        return appendData(dataView.data(), dataView.size());
    }

    Data& append(std::string_view str) noexcept {
        return appendData(reinterpret_cast<const Byte*>(str.data()), str.length());
    }

    Data& append(const Data& other) noexcept {
        if (!other.empty()) {
            appendData(other.rawData_.get(), other.length_);
        }
        return *this;
    }

    Data& append(DataPtr otherData) noexcept {
        if (!otherData) {
            return *this;
        }
        if (empty() && otherData->capacity_ > 0) {
            rawData_ = std::move(otherData->rawData_);
            length_ = otherData->length_;
            capacity_ = otherData->capacity_;
            otherData->reset();
        } else {
            append(*otherData);
        }
        otherData.reset();
        return *this;
    }

    Data operator+(const Data& other) const noexcept {
        Data result = *this;
        result.append(other);
        return result;
    }

    Data& operator+=(const Data& other) noexcept {
        return append(other);
    }

    [[nodiscard]] bool empty() const noexcept {
        return !rawData_ || length_ == 0;
    }

    [[nodiscard]] bool isFull() const noexcept {
        return !empty() && length_ == capacity_;
    }

    [[nodiscard]] uint64_t capacity() const noexcept {
        return capacity_;
    }

    [[nodiscard]] uint64_t length() const noexcept {
        return length_;
    }

    [[nodiscard]] uint64_t size() const noexcept {
        return length_;
    }

    [[nodiscard]] Byte* data() noexcept {
        return rawData_.get();
    }

    [[nodiscard]] const Byte* data() const noexcept {
        return rawData_.get();
    }

    [[nodiscard]] DataView view() const noexcept {
        return rawData_ ? DataView(rawData_.get(), length_) : DataView{};
    }

    [[nodiscard]] DataView view(uint64_t pos) const noexcept {
        return (pos < length_ && rawData_) ? DataView(rawData_.get() + pos, length_ - pos)
                                           : DataView{};
    }

    [[nodiscard]] DataView view(uint64_t pos, uint64_t len) const noexcept {
        return (pos < length_ && rawData_)
                   ? DataView(rawData_.get() + pos, std::min(len, length_ - pos))
                   : DataView{};
    }

    [[nodiscard]] std::string_view toStringView() const noexcept {
        return rawData_ ? std::string_view(reinterpret_cast<const char*>(rawData_.get()), length_)
                        : std::string_view{};
    }

    [[nodiscard]] std::string toString() const noexcept {
        return std::string(toStringView());
    }

    [[nodiscard]] Byte* begin() noexcept {
        return rawData_.get();
    }

    [[nodiscard]] const Byte* begin() const noexcept {
        return rawData_.get();
    }

    [[nodiscard]] Byte* end() noexcept {
        return rawData_ ? rawData_.get() + length_ : nullptr;
    }

    [[nodiscard]] const Byte* end() const noexcept {
        return rawData_ ? rawData_.get() + length_ : nullptr;
    }

    [[nodiscard]] const Byte* cend() const noexcept {
        return rawData_ ? rawData_.get() + length_ : nullptr;
    }

    [[nodiscard]] std::strong_ordering operator<=>(const Data& other) const noexcept {
        if (auto cmp = length_ <=> other.length_; cmp != 0) {
            return cmp;
        }
        return std::lexicographical_compare_three_way(begin(), end(), other.begin(), other.end());
    }

    [[nodiscard]] bool operator==(const Data& other) const noexcept {
        return length_ == other.length_ && std::ranges::equal(view(), other.view());
    }

private:
    [[nodiscard]] bool allocate(uint64_t size) noexcept {
        rawData_ = std::unique_ptr<Byte[]>(new (std::nothrow) Byte[size]);
        if (rawData_) {
            capacity_ = size;
            return true;
        }
        return false;
    }

    void copyFrom(const Data& other) noexcept {
        if (other.length_ > 0 && rawData_) {
            std::copy_n(other.rawData_.get(), other.length_, rawData_.get());
            length_ = other.length_;
        }
    }

    [[nodiscard]] bool ensureCapacity(uint64_t requiredSize) noexcept {
        if (capacity_ >= requiredSize) {
            return true;
        }
        auto newSize = static_cast<uint64_t>(static_cast<double>(requiredSize) * 1.5);
        auto newBuffer = std::unique_ptr<Byte[]>(new (std::nothrow) Byte[newSize]);
        if (!newBuffer) {
            return false;
        }
        if (rawData_) {
            std::copy_n(rawData_.get(), length_, newBuffer.get());
        }
        rawData_ = std::move(newBuffer);
        capacity_ = newSize;
        return true;
    }

    Data& appendData(const Byte* data, std::size_t size) noexcept {
        if (data && size > 0 && ensureCapacity(length_ + static_cast<uint64_t>(size))) {
            std::copy_n(data, size, rawData_.get() + length_);
            length_ += static_cast<uint64_t>(size);
        }
        return *this;
    }

private:
    uint64_t capacity_ = 0;
    uint64_t length_ = 0;
    std::unique_ptr<Byte[]> rawData_;
};

// 数据包结构
struct DataPacket {
    DataPtr data = nullptr;
    int64_t offset = 0;
    std::string tag;
    uint64_t seekEpoch = 0; // seek 纪元，每次 seek + 1，用于全链路隔离旧包

    DataPacket() = default;

    explicit DataPacket(uint64_t size) : data(std::make_unique<Data>(size)) {}

    DataPacket(const DataPacket&) = delete;
    DataPacket& operator=(const DataPacket&) = delete;

    DataPacket(DataPacket&& other) noexcept
        : data(std::move(other.data)),
          offset(other.offset),
          tag(std::move(other.tag)),
          seekEpoch(other.seekEpoch) {}

    DataPacket& operator=(DataPacket&& other) noexcept {
        data = std::move(other.data);
        offset = other.offset;
        tag = std::move(other.tag);
        seekEpoch = other.seekEpoch;
        return *this;
    }

    [[nodiscard]] uint64_t length() const noexcept {
        return data ? data->length() : 0;
    }

    [[nodiscard]] bool empty() const noexcept {
        return length() == 0;
    }
};

} // namespace slark

template <>
struct std::formatter<slark::Data> {
    constexpr auto parse(format_parse_context& ctx) {
        return ctx.begin();
    }

    auto format(const slark::Data& data, format_context& ctx) const {
        return std::format_to(ctx.out(),
                              "Data(length={}, capacity={}, ptr={})",
                              data.length(),
                              data.capacity(),
                              static_cast<const void*>(data.data()));
    }
};
