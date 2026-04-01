//
// Created by Nevermore on 2023/6/15.
// slark StringUtil
// Copyright (c) 2023 Nevermore All rights reserved.
//
#pragma once
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
};

} // namespace slark
