//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//
#pragma once

#include <format>
#include <string>
#include <string_view>

namespace slark {

class DebugLines {
 public:
    template <typename T>
    void add(std::string_view label, const T& value) {
        appendLine(label, std::format("{}", value));
    }

    template <typename T>
    void add(std::string_view label, const T& value, std::string_view unit) {
        appendLine(label, std::format("{} {}", value, unit));
    }

    void append(std::string_view block) {
        lines_.append(block);
    }

    [[nodiscard]] std::string take() {
        return std::move(lines_);
    }

 private:
    void appendLine(std::string_view label, std::string_view value) {
        lines_.append(label);
        lines_.append(": ");
        lines_.append(value);
        lines_.push_back('\n');
    }

    std::string lines_;
};

}  // namespace slark
