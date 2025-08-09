//
// Created by Nevermore on 2025/4/25.
//
#pragma once
#include <cstdint>
#include <optional>

namespace slark {

struct Range {
    std::optional<uint64_t> pos{std::nullopt};
    std::optional<int64_t> size{std::nullopt};

    Range() = default;

    Range(uint64_t position, int64_t length)
        : pos(position), size(length) {}

    /// to the end of file
    explicit Range(uint64_t position)
        : pos(position), size(std::nullopt) {

    }

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
        if (!size.has_value() ) {
            return INT64_MAX;
        }
        return pos.value() + static_cast<uint64_t>(size.value()) - 1;
    }

    [[nodiscard]] std::string toString() const noexcept {
        if (!isValid()) {
            return "Invalid Range";
        }
        if (!size.has_value()) {
            return "Range: [" + std::to_string(pos.value()) + ", end)";
        }
        return "Range: [" + std::to_string(pos.value()) + ", " + std::to_string(end()) + "]";
    }

    [[nodiscard]] std::string toHeaderString() const noexcept {
        if (!pos.has_value()) {
            return "bytes=0-";
        }
        if (!size.has_value()) {
            return "bytes=" + std::to_string(pos.value()) + "-";
        }
        return "bytes=" + std::to_string(pos.value()) + "-" + std::to_string(end());
    }
};

}

