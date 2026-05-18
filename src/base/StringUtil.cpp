//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//
#include "StringUtil.h"

#include <algorithm>
#include <functional>
#include <ranges>
#include <sstream>

namespace slark {

std::vector<std::string_view> StringUtil::split(std::string_view stringView,
                                                std::string_view delimiter) noexcept {
    auto res = stringView | std::ranges::views::split(delimiter) |
               std::views::filter([](auto&& range) {
                   return !range.empty();
               }) |
               std::ranges::views::transform([](auto&& range) {
                   return std::string_view(range);
               });

    return {res.begin(), res.end()};
}

std::string StringUtil::removeSpace(std::string_view view) noexcept {
    std::string result;
    std::ranges::copy_if(view, std::back_inserter(result), [](char ch) {
        return !std::isspace(static_cast<unsigned char>(ch));
    });
    return result;
}

} // namespace slark
