//
// Created by Nevermore on 2024/12/6.
// slark UrlParseTest
// Copyright (c) 2024 Nevermore All rights reserved.
//
#include "Url.hpp"

#include <gtest/gtest.h>
#include <iostream>

using namespace slark::http;

void TestUrlParsing(const std::string& url,
                    const std::string& expectedScheme,
                    const std::string& expectedUserInfo,
                    const std::string& expectedHost,
                    const std::string& expectedPort,
                    const std::string& expectedPath,
                    const std::string& expectedQuery,
                    const std::string& expectedFragment) {
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

TEST(UrlParseTest, BasicUrls) {
    TestUrlParsing("http://example.com", "http", "", "example.com", "80", "/", "", "");
    TestUrlParsing("http://example.com/path/to/resource",
                   "http",
                   "",
                   "example.com",
                   "80",
                   "/path/to/resource",
                   "",
                   "");
    TestUrlParsing("http://example.com/path/to/resource?query=param",
                   "http",
                   "",
                   "example.com",
                   "80",
                   "/path/to/resource",
                   "query=param",
                   "");
    TestUrlParsing("http://example.com/path/to/resource?query1=param1&query2=param2",
                   "http",
                   "",
                   "example.com",
                   "80",
                   "/path/to/resource",
                   "query1=param1&query2=param2",
                   "");
    TestUrlParsing("http://example.com:8080", "http", "", "example.com", "8080", "/", "", "");
    TestUrlParsing("http://example.com/path/to/resource#section",
                   "http",
                   "",
                   "example.com",
                   "80",
                   "/path/to/resource",
                   "",
                   "section");
    TestUrlParsing("http://example.com/path/to/resource?query=param#section",
                   "http",
                   "",
                   "example.com",
                   "80",
                   "/path/to/resource",
                   "query=param",
                   "section");
    TestUrlParsing("http://username:password@example.com",
                   "http",
                   "username:password",
                   "example.com",
                   "80",
                   "/",
                   "",
                   "");
    TestUrlParsing("http://username:password@example.com:8080",
                   "http",
                   "username:password",
                   "example.com",
                   "8080",
                   "/",
                   "",
                   "");
    TestUrlParsing("http://username:password@example.com:8080/path/to/resource?query=param#section",
                   "http",
                   "username:password",
                   "example.com",
                   "8080",
                   "/path/to/resource",
                   "query=param",
                   "section");
}

TEST(UrlParseTest, IPv6Urls) {
    // IPv6地址在内部存储时不包含方括号
    TestUrlParsing("http://[2001:db8::1]:8080/path/to/resource",
                   "http",
                   "",
                   "2001:db8::1",
                   "8080",
                   "/path/to/resource",
                   "",
                   "");
    TestUrlParsing("http://[2001:db8::1]:8080/path/to/resource?query=param",
                   "http",
                   "",
                   "2001:db8::1",
                   "8080",
                   "/path/to/resource",
                   "query=param",
                   "");
    TestUrlParsing("http://[2001:db8::1]:8080/path/to/resource?query=param#section",
                   "http",
                   "",
                   "2001:db8::1",
                   "8080",
                   "/path/to/resource",
                   "query=param",
                   "section");
}

TEST(UrlParseTest, HttpsUrls) {
    TestUrlParsing("https://example.com", "https", "", "example.com", "443", "/", "", "");
    TestUrlParsing("https://example.com:8443", "https", "", "example.com", "8443", "/", "", "");
}

TEST(UrlParseTest, SpecialCharacters) {
    TestUrlParsing("http://example.com/path/to/res@urce?query=pa%20ram&another=param#sec!tion",
                   "http",
                   "",
                   "example.com",
                   "80",
                   "/path/to/res@urce",
                   "query=pa%20ram&another=param",
                   "sec!tion");
}

TEST(UrlParseTest, InvalidUrls) {
    Url invalidUrl1("not-a-valid-url");
    EXPECT_FALSE(invalidUrl1.isValid());

    Url invalidUrl2("://missing-scheme.com");
    EXPECT_FALSE(invalidUrl2.isValid());
}

TEST(UrlParseTest, PortNumber) {
    Url url1("http://example.com");
    EXPECT_EQ(url1.portNumber(), 80);

    Url url2("https://example.com");
    EXPECT_EQ(url2.portNumber(), 443);

    Url url3("http://example.com:8080");
    EXPECT_EQ(url3.portNumber(), 8080);

    Url url4("https://example.com:8443");
    EXPECT_EQ(url4.portNumber(), 8443);
}

TEST(UrlParseTest, IsSupportScheme) {
    Url httpUrl("http://example.com");
    EXPECT_TRUE(httpUrl.isSupportScheme());

    Url httpsUrl("https://example.com");
    EXPECT_TRUE(httpsUrl.isSupportScheme());

    Url ftpUrl("ftp://example.com");
    EXPECT_FALSE(ftpUrl.isSupportScheme());
}