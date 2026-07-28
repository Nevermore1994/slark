#include "HttpResponseParser.h"

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <unordered_map>

#include "Data.h"
#include "StringUtil.h"

using namespace slark;
using namespace slark::http;

namespace {

struct ParseResult {
    HttpStatusCode statusCode = HttpStatusCode::Unknown;
    std::unordered_map<std::string, std::string> headers;
    std::string body;
    bool completed = false;
    bool errored = false;
    Error errorCode = Error::Success;
};

class ParserTestHelper {
 public:
    HttpResponseParser::Callbacks callbacks;
    ParseResult result;

    ParserTestHelper() {
        callbacks.onStatusLine = [this](HttpStatusCode code, const std::string& reason) {
            result.statusCode = code;
            result.reasonPhrase = reason;
        };
        callbacks.onHeaders = [this](std::unordered_map<std::string, std::string>&& h) {
            result.headers = std::move(h);
        };
        callbacks.onBodyData = [this](const Byte* data, size_t size) {
            result.body.append(reinterpret_cast<const char*>(data), size);
        };
        callbacks.onCompleted = [this]() {
            result.completed = true;
        };
        callbacks.onError = [this](Error code) {
            result.errored = true;
            result.errorCode = code;
        };
        parser = std::make_unique<HttpResponseParser>(callbacks);
    }

    void feed(const std::string& data) {
        parser->feed(reinterpret_cast<const Byte*>(data.data()), data.size());
    }

    bool isCompleted() const {
        return parser->isCompleted();
    }

    bool hasError() const {
        return parser->hasError();
    }

 private:
    std::unique_ptr<HttpResponseParser> parser;
    std::string reasonPhrase;
};

}  // namespace

TEST(HttpResponseParserTest, ParseSimpleResponse) {
    ParserTestHelper helper;
    helper.feed("HTTP/1.1 200 OK\r\nContent-Length: 5\r\nContent-Type: text/plain\r\n\r\nHello");
    EXPECT_TRUE(helper.isCompleted());
    EXPECT_FALSE(helper.hasError());
    EXPECT_TRUE(helper.result.completed);
    EXPECT_EQ(helper.result.statusCode, HttpStatusCode::OK);
    EXPECT_EQ(helper.result.body, "Hello");
    EXPECT_EQ(helper.result.headers["Content-Type"], "text/plain");
}

TEST(HttpResponseParserTest, ParseChunkedResponse) {
    ParserTestHelper helper;
    helper.feed("HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n");
    EXPECT_FALSE(helper.isCompleted());
    helper.feed("5\r\nHello\r\n0\r\n\r\n");
    EXPECT_TRUE(helper.isCompleted());
    EXPECT_EQ(helper.result.body, "Hello");
}

TEST(HttpResponseParserTest, ParseChunkedWithMultipleChunks) {
    ParserTestHelper helper;
    helper.feed("HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n");
    helper.feed("6\r\nHello \r\n6\r\nWorld!\r\n0\r\n\r\n");
    EXPECT_TRUE(helper.isCompleted());
    EXPECT_EQ(helper.result.body, "Hello World!");
}

TEST(HttpResponseParserTest, ParseHeadersOnly) {
    ParserTestHelper helper;
    helper.feed("HTTP/1.1 204 No Content\r\nContent-Length: 0\r\n\r\n");
    EXPECT_TRUE(helper.isCompleted());
    EXPECT_TRUE(helper.result.body.empty());
}

TEST(HttpResponseParserTest, ParseNoContentLengthResponse) {
    ParserTestHelper helper;
    helper.feed("HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\n\r\n");
    EXPECT_FALSE(helper.isCompleted());
    helper.feed("partial data");
    EXPECT_FALSE(helper.isCompleted());
    helper.parser->onEof();
    EXPECT_TRUE(helper.isCompleted());
    EXPECT_EQ(helper.result.body, "partial data");
}

TEST(HttpResponseParserTest, Reset) {
    ParserTestHelper helper;
    helper.feed("HTTP/1.1 200 OK\r\nContent-Length: 3\r\n\r\nabc");
    EXPECT_TRUE(helper.isCompleted());

    helper.parser->reset();
    EXPECT_FALSE(helper.isCompleted());
    EXPECT_EQ(helper.result.statusCode, HttpStatusCode::Unknown);
}

TEST(HttpResponseParserTest, ParseMultipleHeaders) {
    ParserTestHelper helper;
    helper.feed("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nX-Custom: "
                "value\r\nContent-Length: 2\r\n\r\n{}");
    EXPECT_TRUE(helper.isCompleted());
    EXPECT_EQ(helper.result.headers["Content-Type"], "application/json");
    EXPECT_EQ(helper.result.headers["X-Custom"], "value");
}

TEST(HttpResponseParserTest, HttpErrorDetection) {
    ParserTestHelper helper;
    helper.feed("HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\n\r\n");
    EXPECT_TRUE(helper.isCompleted());
    EXPECT_TRUE(helper.parser->isHttpError());
}

TEST(HttpResponseParserTest, InformationalStatusNotError) {
    ParserTestHelper helper;
    helper.feed("HTTP/1.1 101 Switching Protocols\r\n\r\n");
    EXPECT_FALSE(helper.parser->isHttpError());
}

TEST(HttpResponseParserTest, MalformedStatusLine) {
    ParserTestHelper helper;
    helper.feed("invalid response line\r\n\r\n");
    EXPECT_TRUE(helper.hasError());
}

TEST(HttpResponseParserTest, ShouldCloseConnection) {
    ParserTestHelper helper;
    helper.feed("HTTP/1.1 200 OK\r\nConnection: close\r\nContent-Length: 0\r\n\r\n");
    EXPECT_TRUE(helper.isCompleted());
    EXPECT_TRUE(helper.parser->shouldCloseConnection());
}

TEST(HttpResponseParserTest, KeepAliveConnection) {
    ParserTestHelper helper;
    helper.feed("HTTP/1.1 200 OK\r\nContent-Length: 0\r\n\r\n");
    EXPECT_TRUE(helper.isCompleted());
    EXPECT_FALSE(helper.parser->shouldCloseConnection());
}

TEST(HttpResponseParserTest, StateTracking) {
    ParserTestHelper helper;
    EXPECT_EQ(helper.parser->state(), HttpResponseParser::State::StatusLine);
    helper.feed("HTTP/1.1 200 OK\r\n");
    EXPECT_EQ(helper.parser->state(), HttpResponseParser::State::Headers);
    helper.feed("Content-Length: 0\r\n\r\n");
    EXPECT_EQ(helper.parser->state(), HttpResponseParser::State::Completed);
}
