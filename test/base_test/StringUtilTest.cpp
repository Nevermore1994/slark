//
// Created by Nevermore on 2023/7/18.
// slark StringTest
// Copyright (c) 2023 Nevermore All rights reserved.
//
#include <gtest/gtest.h>
#include <algorithm>
#include "StringUtil.h"

using namespace slark::string;

TEST(spilt1, string) {
    std::string s1 = "0,1,2,3,4,5,6,7,8,9,";
    auto spiltStrs = spiltString(s1, ",");
    for(size_t i = 0; i < spiltStrs.size(); i++) {
        ASSERT_EQ(spiltStrs[i], std::to_string(i));
    }
}

TEST(spilt3, string) {
    std::string s1 = "https://baidu.com";
    auto spiltStrs = spiltString(s1, "/");
    ASSERT_EQ(spiltStrs.size(), 2);
}

TEST(spilt4, string) {
    std::string s1 = "The required PROPERTY option is immediately followed by the name of the property to set. Remaining arguments are used to compose the property value in the form of a semicolon-separated list. If the APPEND option is given the list is appended to any existing property value. If the APPEND_STRING option is given the string is append to any existing property value as string, i.e. it results in a longer string and not a list of strings.";
    auto spiltStrs = spiltString(s1, " ");
    std::for_each(spiltStrs.begin(), spiltStrs.end(), [](const auto& view){
        std::cout << view << std::endl;
    });
    ASSERT_EQ(spiltStrs.size(), 76);
}

TEST(spilt5, string) {
    std::string_view s1 = "https://baidu.com";
    auto spiltStrs = spiltStringView(s1, "/");
    std::for_each(spiltStrs.begin(), spiltStrs.end(), [](const auto& view){
        std::cout << view << std::endl;
    });
    ASSERT_EQ(spiltStrs.size(), 2);
}

TEST(spilt6, string) {
    std::string s1 = "The required PROPERTY option is immediately followed by the name of the property to set. Remaining arguments are used to compose the property value in the form of a semicolon-separated list. If the APPEND option is given the list is appended to any existing property value. If the APPEND_STRING option is given the string is append to any existing property value as string, i.e. it results in a longer string and not a list of strings.";
    auto spiltStrs = spiltString(s1, " ");
    std::for_each(spiltStrs.begin(), spiltStrs.end(), [](const auto& view){
        std::cout << view << std::endl;
    });
    ASSERT_EQ(spiltStrs.size(), 76);
}
