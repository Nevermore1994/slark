//
// Created by Nevermore on 2025/4/25.
//
#pragma once
#include <cstdint>

namespace slark {

struct Range {
    uint64_t pos{};
    int64_t size{kRangeInvalid};

    [[nodiscard]] bool isValid() const noexcept {
        return size != kRangeInvalid;
    }

    [[nodiscard]] uint64_t end() const noexcept {
        return pos + static_cast<uint64_t>(size);
    }
private:
    static constexpr int64_t kRangeInvalid = -1;
};

}

