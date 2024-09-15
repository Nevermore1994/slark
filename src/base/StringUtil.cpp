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

std::vector<std::string_view> spiltString(std::string_view stringView, std::string_view delimiter) {
    auto res = stringView | std::ranges::views::split(delimiter) | std::views::filter([](auto&& range) {
        return !range.empty();
    }) | std::ranges::views::transform([](auto&& range) {
        return std::string_view(range);
    });
    
    return {res.begin(), res.end()};
}


} // slark