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

std::string StringUtil::extractHostFromUri(std::string_view uri) noexcept {
    auto sep = uri.find("://");
    if (sep == std::string_view::npos) {
        return {};
    }
    auto start = sep + 3;
    auto end = uri.find_first_of("/?#", start);
    return std::string(
        uri.substr(start, end == std::string_view::npos ? std::string_view::npos : end - start));
}

std::string StringUtil::removeSpace(std::string_view view) noexcept {
    std::string result;
    std::ranges::copy_if(view, std::back_inserter(result), [](char ch) {
        return !std::isspace(static_cast<unsigned char>(ch));
    });
    return result;
}

std::string StringUtil::toSafeLogString(std::string_view sv, size_t maxLen) noexcept {
    std::string result;
    result.reserve(std::min(sv.size(), maxLen) * 2);

    size_t i = 0;
    bool truncated = false;
    for (auto c : sv) {
        if (i >= maxLen) {
            truncated = true;
            break;
        }
        auto uc = static_cast<unsigned char>(c);
        // 可打印 ASCII 范围，直接输出
        if (uc >= 0x20 && uc <= 0x7E) {
            result.push_back(c);
        } else if (c == '\r' || c == '\n' || c == '\t') {
            // 常见控制字符用转义
            result.push_back('\\');
            result.push_back(c == '\r' ? 'r' : c == '\n' ? 'n' : 't');
        } else {
            // 非法字节用 \xHH 转义
            constexpr char kHex[] = "0123456789ABCDEF";
            result.push_back('\\');
            result.push_back('x');
            result.push_back(kHex[uc >> 4]);
            result.push_back(kHex[uc & 0x0F]);
            i += 3;  // 额外补偿 \x 的 3 个字符
        }
        ++i;
    }

    if (truncated) {
        result += "...(truncated)";
    }
    return result;
}

}  // namespace slark
