//
// Created by Nevermore on 2023/6/15.
// slark StringUtil
// Copyright (c) 2023 Nevermore All rights reserved.
//
#pragma once
#include <vector>
#include <string>
#include <string_view>

namespace slark {

struct StringUtil {
    static std::vector<std::string_view> split(std::string_view stringView, std::string_view delimiter) noexcept;
    [[maybe_unused]] static std::string& removePrefix(std::string& str, char c) noexcept;
};


} // slark
