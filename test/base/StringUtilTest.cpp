//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//
#include "StringUtil.h"

#include <algorithm>
#include <gtest/gtest.h>

using namespace slark;

TEST(String, spilt1) {
    std::string s1 = "0,1,2,3,4,5,6,7,8,9,";
    auto spiltStrs = StringUtil::split(s1, ",");
    for (size_t i = 0; i < spiltStrs.size(); i++) {
        ASSERT_EQ(spiltStrs[i], std::to_string(i));
    }
}

TEST(String, spilt3) {
    std::string s1 = "https://baidu.com";
    auto spiltStrs = StringUtil::split(s1, "/");
    ASSERT_EQ(spiltStrs.size(), 2);
}

TEST(String, spilt4) {
    std::string s1 = "The required PROPERTY option is immediately followed by the name of the "
                     "property to set. Remaining arguments are used to compose the property value "
                     "in the form of a semicolon-separated list. If the APPEND option is given the "
                     "list is appended to any existing property value. If the APPEND_STRING option "
                     "is given the string is append to any existing property value as string, i.e. "
                     "it results in a longer string and not a list of strings.";
    auto spiltStrs = StringUtil::split(s1, " ");
    std::for_each(spiltStrs.begin(), spiltStrs.end(), [](const auto& view) {
        // std::cout << view << std::endl;
    });
    ASSERT_EQ(spiltStrs.size(), 76);
}

TEST(String, spilt5) {
    std::string_view s1 = "https://baidu.com";
    auto spiltStrs = StringUtil::split(s1, std::string_view("/"));
    std::for_each(spiltStrs.begin(), spiltStrs.end(), [](const auto& view) {
        // std::cout << view << std::endl;
    });
    ASSERT_EQ(spiltStrs.size(), 2);
}

TEST(String, spilt6) {
    std::string s1 = "The required PROPERTY option is immediately followed by the name of the "
                     "property to set. Remaining arguments are used to compose the property value "
                     "in the form of a semicolon-separated list. If the APPEND option is given the "
                     "list is appended to any existing property value. If the APPEND_STRING option "
                     "is given the string is append to any existing property value as string, i.e. "
                     "it results in a longer string and not a list of strings.";
    auto spiltStrs = StringUtil::split(s1, " ");
    std::for_each(spiltStrs.begin(), spiltStrs.end(), [](const auto& view) {
        // std::cout << view << std::endl;
    });
    ASSERT_EQ(spiltStrs.size(), 76);
}

TEST(String, RemoveSpace) {
    EXPECT_EQ(StringUtil::removeSpace("  hello world  "), "helloworld");
    EXPECT_EQ(StringUtil::removeSpace("no spaces"), "nospaces");
    EXPECT_EQ(StringUtil::removeSpace(""), "");
    EXPECT_EQ(StringUtil::removeSpace("   "), "");
    EXPECT_EQ(StringUtil::removeSpace("\t\n\r"), "");
}

TEST(String, StartsWith) {
    EXPECT_TRUE(StringUtil::startsWith("hello world", "hello"));
    EXPECT_FALSE(StringUtil::startsWith("hello world", "world"));
    EXPECT_TRUE(StringUtil::startsWith("hello", ""));
    EXPECT_FALSE(StringUtil::startsWith("", "hello"));
}

TEST(String, EndsWith) {
    EXPECT_TRUE(StringUtil::endsWith("hello world", "world"));
    EXPECT_FALSE(StringUtil::endsWith("hello world", "hello"));
    EXPECT_TRUE(StringUtil::endsWith("hello", ""));
    EXPECT_FALSE(StringUtil::endsWith("", "world"));
}

TEST(String, Contains) {
    EXPECT_TRUE(StringUtil::contains("hello world", "lo wo"));
    EXPECT_FALSE(StringUtil::contains("hello world", "xyz"));
    EXPECT_TRUE(StringUtil::contains("hello", ""));
    EXPECT_FALSE(StringUtil::contains("", "hello"));
}

TEST(String, ToLower) {
    EXPECT_EQ(StringUtil::toLower("HELLO"), "hello");
    EXPECT_EQ(StringUtil::toLower("Hello World"), "hello world");
    EXPECT_EQ(StringUtil::toLower("already lower"), "already lower");
    EXPECT_EQ(StringUtil::toLower(""), "");
    EXPECT_EQ(StringUtil::toLower("123ABC"), "123abc");
}

TEST(String, IEquals) {
    EXPECT_TRUE(StringUtil::iequals("Hello", "hello"));
    EXPECT_TRUE(StringUtil::iequals("WORLD", "world"));
    EXPECT_TRUE(StringUtil::iequals("", ""));
    EXPECT_FALSE(StringUtil::iequals("hello", "world"));
    EXPECT_FALSE(StringUtil::iequals("hello", "hello!"));
    EXPECT_TRUE(StringUtil::iequals("Content-Type", "content-type"));
}

TEST(String, FromCharsInt) {
    auto v = StringUtil::fromChars<int32_t>("42");
    ASSERT_TRUE(v.has_value());
    EXPECT_EQ(*v, 42);

    auto neg = StringUtil::fromChars<int32_t>("-100");
    ASSERT_TRUE(neg.has_value());
    EXPECT_EQ(*neg, -100);

    auto invalid = StringUtil::fromChars<int32_t>("abc");
    EXPECT_FALSE(invalid.has_value());

    auto empty = StringUtil::fromChars<int32_t>("");
    EXPECT_FALSE(empty.has_value());
}

TEST(String, FromCharsUint64) {
    auto v = StringUtil::fromChars<uint64_t>("18446744073709551615");
    ASSERT_TRUE(v.has_value());
    EXPECT_EQ(*v, UINT64_MAX);
}

TEST(String, FromCharsHex) {
    auto v = StringUtil::fromChars<uint32_t>("FF", 16);
    ASSERT_TRUE(v.has_value());
    EXPECT_EQ(*v, 255u);

    auto v2 = StringUtil::fromChars<uint32_t>("1A2B", 16);
    ASSERT_TRUE(v2.has_value());
    EXPECT_EQ(*v2, 0x1A2Bu);
}

TEST(String, FromCharsFloat) {
    auto v = StringUtil::fromChars<float>("3.14");
    ASSERT_TRUE(v.has_value());
    EXPECT_NEAR(*v, 3.14f, 0.001f);

    auto invalid = StringUtil::fromChars<float>("xyz");
    EXPECT_FALSE(invalid.has_value());
}

TEST(String, FromCharsDouble) {
    auto v = StringUtil::fromChars<double>("2.718281828");
    ASSERT_TRUE(v.has_value());
    EXPECT_NEAR(*v, 2.718281828, 1e-9);
}

TEST(String, FromCharsWithPosCount) {
    // "prefix:42" 从 pos=7 取 2 个字符
    auto v = StringUtil::fromChars<int32_t>("prefix:42", 7, 2);
    ASSERT_TRUE(v.has_value());
    EXPECT_EQ(*v, 42);

    // pos 越界
    auto invalid = StringUtil::fromChars<int32_t>("abc", 10, 2);
    EXPECT_FALSE(invalid.has_value());
}
