//
// Created by Nevermore on 2022/5/27.
// slark MediaUtility
// Copyright (c) 2022 Nevermore All rights reserved.
//

#include <regex>

namespace slark {

bool isUrl(const std::string& path) {
    constexpr std::string_view kHttpRegex = "^(http|https):\\/\\/.*";
    std::regex regex(kHttpRegex.data());
    return std::regex_match(path, regex);
}



}//end namespace slark
