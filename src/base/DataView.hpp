// Copyright (c) 2026 Nevermore-. All rights reserved.
#pragma once

#include <algorithm>
#include <compare>
#include <format>
#include <iterator>
#include <limits>
#include <span>
#include <string_view>
#include <vector>

#include "Error.h"
#include "Range.h"
#include "StringUtil.h"
#include "Types.h"

namespace slark {

class DataView {
 public:
    using value_type = Byte;
    using size_type = std::size_t;
    using difference_type = std::ptrdiff_t;
    using const_pointer = const Byte*;
    using const_reference = const Byte&;
    using iterator = const Byte*;
    using const_iterator = const Byte*;

    static constexpr size_type npos = std::numeric_limits<size_type>::max();

    constexpr DataView() noexcept = default;

    constexpr DataView(const Byte* data, size_type size) noexcept : span_(data, size) {}

    constexpr explicit DataView(ConstByteSpan span) noexcept : span_(span) {}

    explicit DataView(std::string_view str) noexcept
        : span_(reinterpret_cast<const Byte*>(str.data()), str.size()) {}

    constexpr DataView(const DataView&) noexcept = default;
    constexpr DataView(DataView&&) noexcept = default;
    constexpr DataView& operator=(const DataView&) noexcept = default;
    constexpr DataView& operator=(DataView&&) noexcept = default;
    ~DataView() = default;

    [[nodiscard]] static DataView fromString(std::string_view str) noexcept {
        return DataView(str);
    }

    [[nodiscard]] static DataView fromSpan(ConstByteSpan span) noexcept {
        return DataView(span);
    }

    template <std::size_t N>
    [[nodiscard]] static constexpr DataView fromArray(const std::array<Byte, N>& arr) noexcept {
        return DataView(arr.data(), N);
    }

    template <std::size_t N>
    [[nodiscard]] static constexpr DataView fromCString(const char (&str)[N]) noexcept {
        return DataView(reinterpret_cast<const Byte*>(str), N - 1);
    }

    [[nodiscard]] constexpr bool empty() const noexcept {
        return span_.empty();
    }

    [[nodiscard]] constexpr size_type size() const noexcept {
        return span_.size();
    }

    [[nodiscard]] constexpr size_type length() const noexcept {
        return span_.size();
    }

    [[nodiscard]] constexpr const_pointer data() const noexcept {
        return span_.data();
    }

    [[nodiscard]] constexpr ConstByteSpan span() const noexcept {
        return span_;
    }

    [[nodiscard]] constexpr const_reference operator[](size_type pos) const noexcept {
        return span_[pos];
    }

    [[nodiscard]] constexpr std::optional<Byte> at(size_type pos) const noexcept {
        return pos < span_.size() ? std::optional<Byte>(span_[pos]) : std::nullopt;
    }

    [[nodiscard]] constexpr const_reference front() const noexcept {
        return span_.front();
    }

    [[nodiscard]] constexpr const_reference back() const noexcept {
        return span_.back();
    }

    [[nodiscard]] constexpr DataView subview(size_type pos = 0,
                                             size_type count = std::dynamic_extent) const noexcept {
        return pos < span_.size() ? DataView(span_.subspan(pos, count)) : DataView{};
    }

    [[nodiscard]] constexpr DataView first(size_type count) const noexcept {
        return DataView(span_.first(std::min(count, span_.size())));
    }

    [[nodiscard]] constexpr DataView last(size_type count) const noexcept {
        return DataView(span_.last(std::min(count, span_.size())));
    }

    [[nodiscard]] std::string_view toStringView() const noexcept {
        return std::string_view(reinterpret_cast<const char*>(span_.data()), span_.size());
    }

    [[nodiscard]] std::string toString() const noexcept {
        return std::string(toStringView());
    }

    template <typename T>
    [[nodiscard]] std::optional<T> fromChars(int base = 10) const noexcept {
        return StringUtil::fromChars<T>(toStringView(), base);
    }

    template <typename T>
    [[nodiscard]] std::optional<T> fromChars(size_t pos,
                                             size_t count,
                                             int base = 10) const noexcept {
        return StringUtil::fromChars<T>(toStringView(), pos, count, base);
    }

    template <typename T>
    [[nodiscard]] constexpr std::optional<T> toNumber() const noexcept {
        if (span_.size() < sizeof(T)) {
            return std::nullopt;
        }
        T result{};
        std::copy_n(reinterpret_cast<const std::byte*>(span_.data()), sizeof(T),
                    reinterpret_cast<std::byte*>(&result));
        return result;
    }

    template <typename T>
    [[nodiscard]] constexpr std::optional<T> toNumberBE() const noexcept {
        auto num = toNumber<T>();
        return num ? std::optional<T>(swapBytes(*num)) : std::nullopt;
    }

    template <typename T>
    [[nodiscard]] constexpr std::optional<T> toNumberLE() const noexcept {
        auto num = toNumber<T>();
#if __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
        return num ? std::optional<T>(swapBytes(*num)) : std::nullopt;
#else
        return num;
#endif
    }

    template <typename T>
    bool readNumberLE(T& value, uint32_t bytes = sizeof(T)) const noexcept {
        static_assert(std::is_unsigned_v<T>);
        if (bytes == 0 || bytes > sizeof(T) || span_.size() < bytes) {
            return false;
        }
        value = 0;
        for (uint32_t i = 0; i < bytes; ++i) {
            value |= static_cast<T>(span_[i]) << (8 * i);
        }
        return true;
    }

    template <typename T>
    bool readNumberBE(T& value, uint32_t bytes = sizeof(T)) const noexcept {
        static_assert(std::is_unsigned_v<T>);
        if (bytes == 0 || bytes > sizeof(T) || span_.size() < bytes) {
            return false;
        }
        value = 0;
        for (uint32_t i = 0; i < bytes; ++i) {
            value |= static_cast<T>(span_[i]) << (8 * (bytes - 1 - i));
        }
        return true;
    }

    [[nodiscard]] constexpr bool startsWith(DataView prefix) const noexcept {
        return prefix.size() <= span_.size() && first(prefix.size()) == prefix;
    }

    [[nodiscard]] constexpr bool endsWith(DataView suffix) const noexcept {
        return suffix.size() <= span_.size() && last(suffix.size()) == suffix;
    }

    [[nodiscard]] constexpr bool contains(DataView needle) const noexcept {
        if (needle.empty()) {
            return true;
        }
        if (needle.size() > span_.size()) {
            return false;
        }
        return std::search(span_.begin(), span_.end(), needle.span_.begin(), needle.span_.end()) !=
               span_.end();
    }

    [[nodiscard]] constexpr size_type find(Byte value, size_type pos = 0) const noexcept {
        if (pos >= span_.size()) {
            return npos;
        }
        auto it = std::find(span_.begin() + static_cast<difference_type>(pos), span_.end(), value);
        return it != span_.end() ? static_cast<size_type>(it - span_.begin()) : npos;
    }

    [[nodiscard]] constexpr size_type find(DataView needle, size_type pos = 0) const noexcept {
        if (pos >= span_.size()) {
            return npos;
        }
        if (needle.empty()) {
            return pos;
        }
        if (needle.size() > span_.size() - pos) {
            return npos;
        }
        auto it = std::search(span_.begin() + static_cast<difference_type>(pos), span_.end(),
                              needle.span_.begin(), needle.span_.end());
        return it != span_.end() ? static_cast<size_type>(it - span_.begin()) : npos;
    }

    [[nodiscard]] constexpr size_type rfind(Byte value, size_type pos = npos) const noexcept {
        if (span_.empty()) {
            return npos;
        }
        auto start = std::min(pos, span_.size() - 1);
        for (auto i = static_cast<difference_type>(start); i >= 0; --i) {
            if (span_[static_cast<size_type>(i)] == value) {
                return static_cast<size_type>(i);
            }
        }
        return npos;
    }

    [[nodiscard]] constexpr size_type find(char c, size_type pos = 0) const noexcept {
        return find(static_cast<Byte>(c), pos);
    }

    [[nodiscard]] constexpr size_type find(std::string_view needle,
                                           size_type pos = 0) const noexcept {
        return find(DataView::fromString(needle), pos);
    }

    [[nodiscard]] constexpr int compare(size_type pos,
                                        size_type count,
                                        DataView other) const noexcept {
        auto sub = subview(pos, count);
        if (sub.empty()) {
            return other.empty() ? 0 : -1;
        }
        auto cmp = std::lexicographical_compare_three_way(sub.begin(), sub.end(), other.begin(),
                                                          other.end());
        return cmp < 0 ? -1 : (cmp > 0 ? 1 : 0);
    }

    [[nodiscard]] constexpr int compare(size_type pos,
                                        size_type count,
                                        std::string_view other) const noexcept {
        return compare(pos, count, DataView::fromString(other));
    }

    [[nodiscard]] constexpr bool startWith(std::string_view prefix) const noexcept {
        return startsWith(DataView::fromString(prefix));
    }

    [[nodiscard]] constexpr DataView substr(size_type pos, size_type count = npos) const noexcept {
        return subview(pos, count == npos ? std::dynamic_extent : count);
    }

    [[nodiscard]] std::vector<DataView> split(std::string_view delimiter) const noexcept;

    [[nodiscard]] std::string removeSpace() const noexcept;

    constexpr DataView& removePrefix(char c) noexcept {
        while (!span_.empty() && static_cast<char>(span_.front()) == c) {
            span_ = span_.subspan(1);
        }
        return *this;
    }

    [[nodiscard]] constexpr bool operator==(DataView other) const noexcept {
        return std::ranges::equal(span_, other.span_);
    }

    [[nodiscard]] constexpr std::strong_ordering operator<=>(DataView other) const noexcept {
        return std::lexicographical_compare_three_way(span_.begin(), span_.end(),
                                                      other.span_.begin(), other.span_.end());
    }

    [[nodiscard]] constexpr iterator begin() const noexcept {
        return span_.data();
    }

    [[nodiscard]] constexpr iterator end() const noexcept {
        return span_.data() + span_.size();
    }

    [[nodiscard]] constexpr const_iterator cbegin() const noexcept {
        return span_.data();
    }

    [[nodiscard]] constexpr const_iterator cend() const noexcept {
        return span_.data() + span_.size();
    }

 private:
    ConstByteSpan span_;

    template <typename T>
    [[nodiscard]] static constexpr T swapBytes(T value) noexcept {
        static_assert(std::is_integral_v<T> || std::is_floating_point_v<T>);
        T result{};
        auto src = reinterpret_cast<const std::byte*>(&value);
        auto dst = reinterpret_cast<std::byte*>(&result);
        for (std::size_t i = 0; i < sizeof(T); ++i) {
            dst[i] = src[sizeof(T) - 1 - i];
        }
        return result;
    }
};

inline DataView operator""_view(const char* str, std::size_t size) noexcept {
    return DataView(reinterpret_cast<const Byte*>(str), size);
}

}  // namespace slark

template <>
struct std::hash<slark::DataView> {
    [[nodiscard]] std::size_t operator()(const slark::DataView& view) const noexcept {
        std::size_t result = 0;
        for (auto byte : view) {
            result ^= std::hash<slark::Byte>{}(byte) + 0x9e3779b9 + (result << 6) + (result >> 2);
        }
        return result;
    }
};

template <>
struct std::formatter<slark::DataView> {
    constexpr auto parse(format_parse_context& ctx) {
        return ctx.begin();
    }

    auto format(const slark::DataView& view, format_context& ctx) const {
        return std::format_to(ctx.out(), "DataView(size={}, data={})", view.size(),
                              static_cast<const void*>(view.data()));
    }
};
