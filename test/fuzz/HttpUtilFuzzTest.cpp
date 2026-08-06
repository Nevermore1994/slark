#include <gtest/gtest.h>

#include <random>
#include <string>
#include <vector>

#include "Data.h"
#include "HttpUtil.hpp"

using namespace slark::http::Util;

TEST(HttpUtilFuzzTest, RandomResponseDataDoesNotCrash) {
    std::mt19937 rng(42);
    std::uniform_int_distribution<uint8_t> byteDist;

    for (int i = 0; i < 1000; ++i) {
        std::uniform_int_distribution<size_t> lenDist(0, 2048);
        size_t len = lenDist(rng);
        std::vector<uint8_t> data(len);
        for (auto& b : data) {
            b = byteDist(rng);
        }
        ParsedResponse response;
        EXPECT_NO_THROW({ parseRawResponse(data.data(), data.size(), response); });
    }
}

TEST(HttpUtilFuzzTest, ChunkedEncodingVariants) {
    std::vector<std::string> chunkedInputs = {
        "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n5\r\nHello\r\n0\r\n\r\n",
        "HTTP/1.1 200 OK\r\nTransfer-Encoding: "
        "chunked\r\n\r\n5\r\nHello\r\n5\r\nWorld\r\n0\r\n\r\n",
        "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n0\r\n\r\n",
        "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n5;ext=1\r\nHello\r\n0\r\n\r\n",
        "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\nG\r\nHello\r\n0\r\n\r\n",
        "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n-1\r\nHello\r\n0\r\n\r\n",
        "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n5\r\nHel",
        "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n5\r\n",
        "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n",
        "HTTP/1.1 200 OK\r\nTransfer-Encoding: "
        "chunked\r\n\r\n5\r\nHello\r\n0\r\n\r\ntrailer:val\r\n",
        "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\nContent-Length: "
        "5\r\n\r\n5\r\nHello\r\n0\r\n\r\n",
        "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\nFFFFFFF\r\nHello\r\n0\r\n\r\n",
        std::string("HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n") +
            std::string(100, '\x00') + "\r\n0\r\n\r\n",
    };
    for (const auto& input : chunkedInputs) {
        ParsedResponse response;
        EXPECT_NO_THROW({ parseRawResponse(input.data(), input.size(), response); });
    }
}

TEST(HttpUtilFuzzTest, MalformedStatusLines) {
    std::vector<std::string> malformedInputs = {
        "",
        "\n",
        "\r\n",
        "HTTP/1.1",
        "HTTP/1.1 200",
        "HTTP/1.1 200 OK",
        "HTTP/1.1 200 OK\r\n",
        "HTTP/1.1 200 OK\r\n\r\n",
        "HTTP/1.1 999 Unknown\r\n\r\n",
        "HTTP/1.1 200\r\nContent-Length: 0\r\n\r\n",
        "HTTP/1.0 200 OK\r\n\r\n",
        "HTTP/2.0 200 OK\r\n\r\n",
        "http/1.1 200 ok\r\n\r\n",
        "NOTHTTP/1.1 200 OK\r\n\r\n",
        "HTTP/1.1 200 OK\r\nContent-Length: abc\r\n\r\n",
        "HTTP/1.1 200 OK\r\nContent-Length: -1\r\n\r\n",
        "HTTP/1.1 200 OK\r\nContent-Length: 9999999999999\r\n\r\n",
        "HTTP/1.1 200 OK\r\nContent-Length: 0\r\nContent-Length: 5\r\n\r\n",
        "HTTP/1.1 200 OK\r\nContent-Length: 10\r\n\r\n12345",
        "HTTP/1.1 200 OK\r\nConnection: keep-alive\r\n\r\n",
        "HTTP/1.1 200 OK\r\nLocation: http://example.com\r\n\r\n",
        "HTTP/1.1 302 Found\r\nLocation: /\r\n\r\n",
        "HTTP/1.1 200 OK\r\n" + std::string(100, 'X') + ": value\r\n\r\n",
    };
    for (const auto& input : malformedInputs) {
        ParsedResponse response;
        EXPECT_NO_THROW({ parseRawResponse(input.data(), input.size(), response); });
    }
}

TEST(HttpUtilFuzzTest, LargeHeaders) {
    std::mt19937 rng(42);
    for (int i = 0; i < 100; ++i) {
        std::string input = "HTTP/1.1 200 OK\r\n";
        std::uniform_int_distribution<size_t> headerLenDist(0, 8192);
        size_t headerLen = headerLenDist(rng);
        std::string headerValue(headerLen, 'A');
        input += "X-Large: " + headerValue + "\r\n";
        input += "Content-Length: 0\r\n\r\n";
        ParsedResponse response;
        EXPECT_NO_THROW({ parseRawResponse(input.data(), input.size(), response); });
    }
}

TEST(HttpUtilFuzzTest, RepeatedHeaders) {
    std::string input = "HTTP/1.1 200 OK\r\n";
    for (int i = 0; i < 200; ++i) {
        input += "X-Header-" + std::to_string(i) + ": value" + std::to_string(i) + "\r\n";
    }
    input += "Content-Length: 0\r\n\r\n";
    ParsedResponse response;
    EXPECT_NO_THROW({ parseRawResponse(input.data(), input.size(), response); });
}
