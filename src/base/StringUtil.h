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

std::vector<std::string_view> spiltString(std::string_view stringView, std::string_view delimiter);

} // slark
