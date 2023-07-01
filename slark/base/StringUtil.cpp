//
// Created by Nevermore on 2023/6/15.
// slark StringUtil
// Copyright (c) 2023 Nevermore All rights reserved.
//
#include "StringUtil.hpp"
#include <sstream>
#include <functional>

namespace Slark {

std::vector<std::string> spiltString(const std::string& str, char delimiter) {
    std::istringstream iss(str);
    std::vector<std::string> res;
    for (std::string item; std::getline(iss, item, delimiter);) {
        if (item.empty()) {
            continue;
        } else {
            res.push_back(std::move(item));
        }
    }
    return res;
}

std::vector<std::string> spiltString(const std::string& str, const std::string& delimiter) {
    auto stringViews = spiltStringView(str, delimiter);
    std::vector<std::string> res;
    std::for_each(stringViews.begin(), stringViews.end(), [&](std::string_view view) {
        res.emplace_back(view);
    });
    return res;
}

std::vector<std::string_view> spiltStringView(const std::string& str, char delimiter) {
    std::vector<std::string_view> res;
    std::string_view stringView(str);
    auto lastPos = stringView.find_first_not_of(delimiter, 0);
    auto pos = stringView.find_first_of(delimiter, lastPos);
    while (pos != std::string::npos || lastPos != std::string::npos) {
        res.push_back(stringView.substr(lastPos, pos - lastPos));
        lastPos = stringView.find_first_not_of(delimiter, pos);
        pos = stringView.find_first_of(delimiter, lastPos);
    }
    return res;
}

std::vector<std::string_view> spiltStringView(const std::string& str, const std::string& delimiter) {
    std::string_view stringView(str);
    size_t posStart = 0, posEnd, delimLength = delimiter.length();
    std::vector<std::string_view> res;

    while ((posEnd = stringView.find(delimiter, posStart)) != std::string::npos) {
        auto token = stringView.substr(posStart, posEnd - posStart);
        posStart = posEnd + delimLength;
        res.push_back(token);
    }

    res.push_back(stringView.substr(posStart));
    return res;
}

} // Slark