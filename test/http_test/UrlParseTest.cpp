//
// Created by Nevermore on 2024/12/6.
// slark RequestTest
// Copyright (c) 2024 Nevermore All rights reserved.
//
#include <iostream>
#include <gtest/gtest.h>
#include "../http/include/Url.h"

using namespace slark::http;

void TestUrlParsing(const std::string& url, const std::string& expectedScheme,
                    const std::string& expectedUserInfo, const std::string& expectedHost,
                    const std::string& expectedPort, const std::string& expectedPath,
                    const std::string& expectedQuery, const std::string& expectedFragment) {
    Url urlObj(url);

    EXPECT_TRUE(urlObj.isValid());
    EXPECT_EQ(urlObj.scheme, expectedScheme);
    EXPECT_EQ(urlObj.userInfo, expectedUserInfo);
    EXPECT_EQ(urlObj.host, expectedHost);
    EXPECT_EQ(urlObj.port, expectedPort);
    EXPECT_EQ(urlObj.path, expectedPath);
    EXPECT_EQ(urlObj.query, expectedQuery);
    EXPECT_EQ(urlObj.fragment, expectedFragment);
}

// Test cases
TEST(UrlParseTest, BasicUrls) {
    TestUrlParsing("http://example.com", "http", "", "example.com", "80", "/", "", "");
    TestUrlParsing("http://example.com/path/to/resource", "http", "", "example.com", "80", "/path/to/resource", "", "");
    TestUrlParsing("http://example.com/path/to/resource?query=param", "http", "", "example.com", "80", "/path/to/resource", "query=param", "");
    TestUrlParsing("http://example.com/path/to/resource?query1=param1&query2=param2", "http", "", "example.com", "80", "/path/to/resource", "query1=param1&query2=param2", "");
    TestUrlParsing("http://example.com:8080", "http", "", "example.com", "8080", "/", "", "");
    TestUrlParsing("http://example.com/path/to/resource#section", "http", "", "example.com", "80", "/path/to/resource", "", "section");
    TestUrlParsing("http://example.com/path/to/resource?query=param#section", "http", "", "example.com", "80", "/path/to/resource", "query=param", "section");
    TestUrlParsing("http://username:password@example.com", "http", "username:password", "example.com", "80", "/", "", "");
    TestUrlParsing("http://username:password@example.com:8080", "http", "username:password", "example.com", "8080", "/", "", "");
    TestUrlParsing("http://username:password@example.com:8080/path/to/resource?query=param#section", "http", "username:password", "example.com", "8080", "/path/to/resource", "query=param", "section");
    TestUrlParsing("http://example.com/path/to/res@urce?query=pa%20ram&another=param#sec!tion", "http", "", "example.com", "80", "/path/to/res@urce", "query=pa%20ram&another=param", "sec!tion");
    TestUrlParsing("http://[2001:db8::1]:8080/path/to/resource", "http", "", "[2001:db8::1]", "8080", "/path/to/resource", "", "");
    TestUrlParsing("http://[2001:db8::1]:8080/path/to/resource?query=param", "http", "", "[2001:db8::1]", "8080", "/path/to/resource", "query=param", "");
    TestUrlParsing("http://[2001:db8::1]:8080/path/to/resource?query=param#section", "http", "", "[2001:db8::1]", "8080", "/path/to/resource", "query=param", "section");
}