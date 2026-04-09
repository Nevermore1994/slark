//
// Created by Nevermore on 2023/6/15.
// slark StringUtil
// Copyright (c) 2023 Nevermore All rights reserved.
//
#pragma once
#include <algorithm>
#include <ranges>
#include <string>
#include <string_view>
#include <vector>

namespace slark {

struct StringUtil {
    static std::vector<std::string_view> split(std::string_view stringView,
                                               std::string_view delimiter) noexcept;

    static std::string removeSpace(std::string_view view) noexcept;

    [[nodiscard]] static bool startsWith(std::string_view str, std::string_view prefix) noexcept {
        return str.starts_with(prefix);
    }

    [[nodiscard]] static bool endsWith(std::string_view str, std::string_view suffix) noexcept {
        return str.ends_with(suffix);
    }

    [[nodiscard]] static bool contains(std::string_view str, std::string_view sub) noexcept {
        return str.find(sub) != std::string_view::npos;
    }

    [[nodiscard]] static std::string toLower(std::string str) noexcept {
        std::ranges::transform(str, str.begin(), [](unsigned char c) {
            return std::tolower(c);
        });
        return str;
    }

    [[nodiscard]] static bool iequals(std::string_view a, std::string_view b) noexcept {
        if (a.size() != b.size()) {
            return false;
        }
        return std::ranges::equal(a, b, [](unsigned char ca, unsigned char cb) {
            return std::tolower(ca) == std::tolower(cb);
        });
    }
};

} // namespace slark
