//
// Created by Nevermore on 2023/6/15.
// slark StringUtil
// Copyright (c) 2023 Nevermore All rights reserved.
//
#include "StringUtil.h"
#include <sstream>
#include <functional>
#include <ranges>

namespace slark::string {

std::vector<std::string> spiltString(const std::string& str, const std::string_view& delimiter) {
    auto res = str | std::views::split(delimiter) | std::views::filter([](auto&& range) {
        return !range.empty();
    }) | std::views::transform([](auto&& range) {
        return std::string(std::string_view(range));
    });
    return std::vector<std::string>(res.begin(), res.end());
}

std::vector<std::string_view> spiltStringView(const std::string_view& stringView, const std::string_view& delimiter) {
    auto res = stringView | std::ranges::views::split(delimiter) | std::views::filter([](auto&& range) {
        return !range.empty();
    }) | std::ranges::views::transform([](auto&& range) {
        return std::string_view(range);
    });
    return std::vector<std::string_view>(res.begin(), res.end());
}


} // slark
