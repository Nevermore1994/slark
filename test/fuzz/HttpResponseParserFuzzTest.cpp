#include <gtest/gtest.h>

#include <random>
#include <string>
#include <vector>

#include "Data.h"
#include "HttpResponseParser.h"

using namespace slark;
using namespace slark::http;

namespace {

void fuzzFeed(HttpResponseParser& parser, const std::string& data) {
    parser.feed(reinterpret_cast<const Byte*>(data.data()), data.size());
}

}  // namespace

TEST(HttpResponseParserFuzzTest, RandomChunkedInput) {
    HttpResponseParser::Callbacks callbacks;
    std::string body;
    callbacks.onBodyData = [&](const Byte* d, size_t s) {
        body.append(reinterpret_cast<const char*>(d), s);
    };
    HttpResponseParser parser(callbacks);

    std::string response = "HTTP/1.1 200 OK\r\nContent-Length: 10\r\n\r\n0123456789";
    std::mt19937 rng(42);
    size_t pos = 0;
    while (pos < response.size()) {
        size_t chunk = 1 + (rng() % std::min<size_t>(5, response.size() - pos));
        fuzzFeed(parser, response.substr(pos, chunk));
        pos += chunk;
    }
    EXPECT_TRUE(parser.isCompleted());
    EXPECT_EQ(body, "0123456789");
}

TEST(HttpResponseParserFuzzTest, InvalidInputDoesNotCrash) {
    HttpResponseParser::Callbacks callbacks;
    bool errored = false;
    callbacks.onError = [&](Error) {
        errored = true;
    };
    HttpResponseParser parser(callbacks);

    std::vector<std::string> invalidInputs = {
        "",
        "\n",
        "garbage data without HTTP",
        "HTTP/1.1",
        "HTTP/1.1 999 Unknown\r\n\r\n",
        "HTTP/1.1 200 OK\r\nContent-Length: abc\r\n\r\n",
        "HTTP/1.1 200 OK\r\n\r\n",
        "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\ninvalid",
        "\x00\x01\x02",
        std::string(10000, 'A'),
    };

    for (const auto& input : invalidInputs) {
        EXPECT_NO_THROW(fuzzFeed(parser, input));
        parser.reset();
    }
}

TEST(HttpResponseParserFuzzTest, RandomChunkedEncoding) {
    HttpResponseParser::Callbacks callbacks;
    std::string body;
    callbacks.onBodyData = [&](const Byte* d, size_t s) {
        body.append(reinterpret_cast<const char*>(d), s);
    };
    HttpResponseParser parser(callbacks);

    std::string response = "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n"
                           "5\r\nHello\r\n5\r\nWorld\r\n0\r\n\r\n";
    std::mt19937 rng(42);
    size_t pos = 0;
    while (pos < response.size()) {
        size_t chunk = 1 + (rng() % std::min<size_t>(8, response.size() - pos));
        fuzzFeed(parser, response.substr(pos, chunk));
        pos += chunk;
    }
    EXPECT_TRUE(parser.isCompleted());
    EXPECT_EQ(body, "HelloWorld");
}

TEST(HttpResponseParserFuzzTest, LargeContentLength) {
    HttpResponseParser::Callbacks callbacks;
    bool errored = false;
    callbacks.onError = [&](Error) {
        errored = true;
    };
    HttpResponseParser parser(callbacks);

    fuzzFeed(parser, "HTTP/1.1 200 OK\r\nContent-Length: 99999999\r\n\r\n");
    EXPECT_FALSE(parser.isCompleted());
    EXPECT_FALSE(errored);
}

TEST(HttpResponseParserFuzzTest, RepeatedResetAndFeed) {
    HttpResponseParser::Callbacks callbacks;
    HttpResponseParser parser(callbacks);

    for (int i = 0; i < 10; ++i) {
        fuzzFeed(parser, "HTTP/1.1 200 OK\r\nContent-Length: 0\r\n\r\n");
        EXPECT_TRUE(parser.isCompleted());
        parser.reset();
        EXPECT_FALSE(parser.isCompleted());
    }
}

TEST(HttpResponseParserFuzzTest, TruncatedHeaders) {
    HttpResponseParser::Callbacks callbacks;
    bool errored = false;
    callbacks.onError = [&](Error e) {
        errored = true;
    };
    HttpResponseParser parser(callbacks);

    fuzzFeed(parser, "HTTP/1.1 200 OK\r\nContent-");
    EXPECT_FALSE(errored);
    parser.onEof();
}

TEST(HttpResponseParserFuzzTest, ManyHeaders) {
    HttpResponseParser::Callbacks callbacks;
    int headerCount = 0;
    callbacks.onHeaders = [&](auto&& h) {
        headerCount = h.size();
    };
    HttpResponseParser parser(callbacks);

    std::string response = "HTTP/1.1 200 OK\r\n";
    for (int i = 0; i < 100; ++i) {
        response += "X-Header-" + std::to_string(i) + ": value" + std::to_string(i) + "\r\n";
    }
    response += "Content-Length: 0\r\n\r\n";
    fuzzFeed(parser, response);
    EXPECT_TRUE(parser.isCompleted());
    EXPECT_EQ(headerCount, 101);  // 100 custom + Content-Length
}
