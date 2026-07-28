//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//
#pragma once
#include <compare>
#include <cstdint>
#include <format>
#include <optional>
#include <string>

namespace slark {

struct Range {
    std::optional<uint64_t> pos{std::nullopt};
    std::optional<int64_t> size{std::nullopt};

    Range() = default;

    Range(uint64_t position, int64_t length) : pos(position), size(length) {}

    /// to the end of file
    explicit Range(uint64_t position) : pos(position), size(std::nullopt) {}

    void moveTo(uint64_t position) noexcept {
        pos = position;
    }

    void shift(int64_t offset) noexcept {
        auto newPos = int64_t(pos.value_or(0)) + offset;
        if (newPos < 0) {
            pos = 0;
        } else {
            pos = newPos;
        }
    }

    [[nodiscard]] bool isValid() const noexcept {
        return pos.has_value();
    }

    [[nodiscard]] uint64_t start() const noexcept {
        if (!isValid()) {
            return 0;
        }
        return pos.value();
    }

    [[nodiscard]] uint64_t end() const noexcept {
        if (!isValid()) {
            return 0;
        }
        if (!size.has_value()) {
            return INT64_MAX;
        }
        return pos.value() + static_cast<uint64_t>(size.value()) - 1;
    }

    [[nodiscard]] bool contains(uint64_t position) const noexcept {
        if (!isValid()) {
            return false;
        }
        return position >= start() && position <= end();
    }

    [[nodiscard]] std::optional<Range> overlap(const Range& other) const noexcept {
        if (!isValid() || !other.isValid()) {
            return std::nullopt;
        }
        auto overlapStart = std::max(start(), other.start());
        auto overlapEnd = std::min(end(), other.end());
        if (overlapStart > overlapEnd) {
            return std::nullopt;
        }
        return Range(overlapStart, static_cast<int64_t>(overlapEnd - overlapStart + 1));
    }

    auto operator<=>(const Range& other) const noexcept {
        if (!isValid() && !other.isValid()) {
            return std::strong_ordering::equal;
        }
        if (!isValid()) {
            return std::strong_ordering::less;
        }
        if (!other.isValid()) {
            return std::strong_ordering::greater;
        }
        if (auto cmp = start() <=>other.start(); cmp != 0) {
            return cmp;
        }
        return end() <=>other.end();
    }

    bool operator==(const Range& other) const noexcept {
        return (*this <=>other) == 0;
    }

    [[nodiscard]] std::string toString() const noexcept {
        if (!isValid()) {
            return "Invalid Range";
        }
        if (!size.has_value()) {
            return std::format("Range: [{}, end)", pos.value());
        }
        return std::format("Range: [{}, {}]", pos.value(), end());
    }

    [[nodiscard]] std::string toHeaderString() const noexcept {
        if (!pos.has_value()) {
            return "bytes=0-";
        }
        if (!size.has_value()) {
            return std::format("bytes={}-", pos.value());
        }
        return std::format("bytes={}-{}", pos.value(), end());
    }
};

}  // namespace slark
