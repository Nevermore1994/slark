#include "Url.h"

#include <gtest/gtest.h>
#include <random>
#include <string>
#include <vector>

using namespace slark::http;

TEST(UrlFuzzTest, RandomStringsDoNotCrash) {
    std::mt19937 rng(42);
    std::uniform_int_distribution<int> charDist(0, 127);

    for (int i = 0; i < 1000; ++i) {
        std::uniform_int_distribution<size_t> lenDist(0, 256);
        size_t len = lenDist(rng);
        std::string input;
        input.reserve(len);
        for (size_t j = 0; j < len; ++j) {
            input.push_back(charDist(rng));
        }
        EXPECT_NO_THROW({
            Url url(input);
            (void)url.isValid();
            (void)url.url();
            (void)url.scheme;
            (void)url.host;
            (void)url.port;
            (void)url.path;
            (void)url.query;
            (void)url.fragment;
            (void)url.portNumber();
            (void)url.isSupportScheme();
        });
    }
}

TEST(UrlFuzzTest, MalformedSchemes) {
    std::vector<std::string> inputs = {
        "",
        "://host.com",
        "http://",
        "https://",
        "://",
        "http:",
        "https:host.com",
        "  http://host.com",
        "http://host.com:99999",
        "http://host.com:-1",
        "http://host.com:abc",
        "http://[::1",
        "http://[invalid]",
        "http://host.com/path with spaces",
        "http://host.com/\x00\x01\x02",
        "http://host.com/\x80\xFF",
        "http://host.com#frag#double",
        "http://host.com?query=value&foo=bar#frag",
        "http://user@host.com",
        "http://user:pass@host.com",
        "http://host.com:0",
        "http://host.com:65536",
        "http://192.168.1.1:8080",
        "HTTP://HOST.COM",
        "HtTp://MiXeD.CaSe",
        "://:",
        "http://[::1]:8080/path?query=1",
        "http://[::1]:99999/path",
        "file:///etc/passwd",
        "ftp://host.com",
        "http://a.b.c.d.e.f.g.h",
        "http://.com",
        "http://host..com",
        "http://host.com.",
    };
    for (const auto& input : inputs) {
        EXPECT_NO_THROW({
            Url url(input);
            (void)url.isValid();
            (void)url.portNumber();
        });
    }
}

TEST(UrlFuzzTest, DeeplyNestedPaths) {
    std::mt19937 rng(42);
    for (int i = 0; i < 100; ++i) {
        std::string path = "http://host.com/";
        std::uniform_int_distribution<int> depthDist(1, 50);
        int depth = depthDist(rng);
        for (int j = 0; j < depth; ++j) {
            path += "dir" + std::to_string(j) + "/";
        }
        path += "file.txt";
        EXPECT_NO_THROW({
            Url url(path);
            (void)url.path;
        });
    }
}

TEST(UrlFuzzTest, LongQueryAndFragment) {
    std::mt19937 rng(42);
    std::uniform_int_distribution<int> charDist(32, 126);
    for (int i = 0; i < 50; ++i) {
        std::string input = "http://host.com/path?";
        std::uniform_int_distribution<size_t> lenDist(0, 4096);
        size_t queryLen = lenDist(rng);
        for (size_t j = 0; j < queryLen; ++j) {
            input.push_back(charDist(rng));
        }
        input += "#frag";
        size_t fragLen = lenDist(rng);
        for (size_t j = 0; j < fragLen; ++j) {
            input.push_back(charDist(rng));
        }
        EXPECT_NO_THROW({
            Url url(input);
            (void)url.query;
            (void)url.fragment;
        });
    }
}
