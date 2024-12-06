//
// Created by Nevermore on 2023/6/15.
// slark StringUtil
// Copyright (c) 2023 Nevermore All rights reserved.
//
#include "StringUtil.h"
#include <sstream>
#include <functional>
#include <ranges>

namespace slark {

std::vector<std::string_view> StringUtil::split(std::string_view stringView, std::string_view delimiter) noexcept {
    auto res = stringView | std::ranges::views::split(delimiter) | std::views::filter([](auto&& range) {
        return !range.empty();
    }) | std::ranges::views::transform([](auto&& range) {
        return std::string_view(range);
    });
    
    return {res.begin(), res.end()};
}

std::string& StringUtil::removePrefix(std::string& str, char c) noexcept {
    if (!str.empty() && str.front() == c) {
        str.erase(0);
    }
    return str;
}


} // slark