//
// Created by Nevermore on 2023/6/15.
// slark StringUtil
// Copyright (c) 2023 Nevermore All rights reserved.
//
#pragma once
#include <vector>
#include <string>
#include <string_view>

namespace slark::string {

std::vector<std::string> spiltString(const std::string& str, const std::string_view& delimiter);

std::vector<std::string_view> spiltStringView(const std::string_view& stringView, const std::string_view& delimiter);

} // slark
