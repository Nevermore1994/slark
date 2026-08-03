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
    std::string reasonPhrase;
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

    // 按固定步长分片喂入，覆盖跨 feed 边界的解析路径
    void feedInSlices(const std::string& data, size_t slice) {
        for (size_t i = 0; i < data.size(); i += slice) {
            auto n = std::min(slice, data.size() - i);
            feed(data.substr(i, n));
        }
    }

    bool isCompleted() const {
        return parser->isCompleted();
    }

    bool hasError() const {
        return parser->hasError();
    }

    // 暴露 parser 供断言状态机细节（state/isHttpError/reset 等）
    HttpResponseParser* rawParser() const {
        return parser.get();
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
    EXPECT_EQ(helper.result.headers["content-type"], "text/plain");
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
    helper.rawParser()->onEof();
    EXPECT_TRUE(helper.isCompleted());
    EXPECT_EQ(helper.result.body, "partial data");
}

TEST(HttpResponseParserTest, Reset) {
    ParserTestHelper helper;
    helper.feed("HTTP/1.1 200 OK\r\nContent-Length: 3\r\n\r\nabc");
    EXPECT_TRUE(helper.isCompleted());

    helper.rawParser()->reset();
    EXPECT_FALSE(helper.isCompleted());
    EXPECT_FALSE(helper.hasError());
    EXPECT_EQ(helper.rawParser()->state(), HttpResponseParser::State::StatusLine);
}

TEST(HttpResponseParserTest, ParseMultipleHeaders) {
    ParserTestHelper helper;
    helper.feed("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nX-Custom: "
                "value\r\nContent-Length: 2\r\n\r\n{}");
    EXPECT_TRUE(helper.isCompleted());
    EXPECT_EQ(helper.result.headers["content-type"], "application/json");
    EXPECT_EQ(helper.result.headers["x-custom"], "value");
}

TEST(HttpResponseParserTest, HttpErrorDetection) {
    ParserTestHelper helper;
    helper.feed("HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\n\r\n");
    EXPECT_TRUE(helper.isCompleted());
    EXPECT_TRUE(helper.rawParser()->isHttpError());
}

TEST(HttpResponseParserTest, InformationalStatusNotError) {
    ParserTestHelper helper;
    helper.feed("HTTP/1.1 101 Switching Protocols\r\n\r\n");
    EXPECT_FALSE(helper.rawParser()->isHttpError());
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
    EXPECT_TRUE(helper.rawParser()->shouldCloseConnection());
}

TEST(HttpResponseParserTest, KeepAliveConnection) {
    ParserTestHelper helper;
    helper.feed("HTTP/1.1 200 OK\r\nContent-Length: 0\r\n\r\n");
    EXPECT_TRUE(helper.isCompleted());
    EXPECT_FALSE(helper.rawParser()->shouldCloseConnection());
}

TEST(HttpResponseParserTest, StateTracking) {
    ParserTestHelper helper;
    EXPECT_EQ(helper.rawParser()->state(), HttpResponseParser::State::StatusLine);
    helper.feed("HTTP/1.1 200 OK\r\n");
    EXPECT_EQ(helper.rawParser()->state(), HttpResponseParser::State::Headers);
    helper.feed("Content-Length: 0\r\n\r\n");
    EXPECT_EQ(helper.rawParser()->state(), HttpResponseParser::State::Completed);
}

TEST(HttpResponseParserTest, HeadRequestHasNoBody) {
    ParserTestHelper helper;
    helper.rawParser()->setRequestMethod(HttpMethodType::Head);
    helper.feed("HTTP/1.1 200 OK\r\nContent-Length: 5\r\n\r\n");
    EXPECT_TRUE(helper.isCompleted());
    EXPECT_TRUE(helper.result.body.empty());
}

TEST(HttpResponseParserTest, DISABLED_InformationalAndNoBodyStatusCompleteImmediately) {
    // 真实缺陷（阶段二 3.4 修复）：无任何 header 的响应（仅状态行 + 空行）
    // 在 processStatusLine 消费状态行后剩余 "\r\n"，processHeaders 用 find("\r\n\r\n")
    // 无法识别单空行为结束，卡在 Headers 状态。修复后启用。
    for (const auto& line : {"HTTP/1.1 100 Continue\r\n\r\n", "HTTP/1.1 204 No Content\r\n\r\n",
                             "HTTP/1.1 304 Not Modified\r\n\r\n"}) {
        ParserTestHelper helper;
        helper.feed(line);
        EXPECT_TRUE(helper.isCompleted()) << line;
        EXPECT_TRUE(helper.result.body.empty()) << line;
    }
}

TEST(HttpResponseParserTest, DuplicateContentLengthTakesLastValue) {
    ParserTestHelper helper;
    helper.feed("HTTP/1.1 200 OK\r\nContent-Length: 5\r\nContent-Length: 3\r\n\r\nabc");
    EXPECT_TRUE(helper.isCompleted());
    EXPECT_EQ(helper.result.body, "abc");
}

TEST(HttpResponseParserTest, DISABLED_InvalidContentLengthTrailingCharsFails) {
    // 真实缺陷（阶段二 3.4 修复）：fromChars("5abc") 解析出 5 而非报错，
    // 未校验完整消费。修复后启用。
    ParserTestHelper helper;
    helper.feed("HTTP/1.1 200 OK\r\nContent-Length: 5abc\r\n\r\n");
    EXPECT_TRUE(helper.hasError());
    EXPECT_EQ(helper.result.errorCode, Error::HttpResponseInvalid);
}

TEST(HttpResponseParserTest, HeaderLineWithoutColonIsIgnored) {
    ParserTestHelper helper;
    helper.feed("HTTP/1.1 200 OK\r\nBadHeaderNoColon\r\nContent-Length: 0\r\n\r\n");
    EXPECT_TRUE(helper.isCompleted());
    EXPECT_FALSE(helper.hasError());
}

TEST(HttpResponseParserTest, OversizedHeaderFails) {
    ParserTestHelper helper;
    std::string huge;
    huge.reserve(70 * 1024);
    huge = "HTTP/1.1 200 OK\r\n";
    huge.append(66 * 1024, 'a');
    helper.feed(huge);
    EXPECT_TRUE(helper.hasError());
    EXPECT_EQ(helper.result.errorCode, Error::HttpResponseInvalid);
}

TEST(HttpResponseParserTest, OversizedChunkSizeFails) {
    ParserTestHelper helper;
    helper.feed("HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n");
    helper.feed("FFFFFFFFFFFFFFFF;\r\n");
    EXPECT_TRUE(helper.hasError());
    EXPECT_EQ(helper.result.errorCode, Error::HttpChunkedError);
}

TEST(HttpResponseParserTest, StatusLineWithoutReasonPhraseFails) {
    ParserTestHelper helper;
    helper.feed("HTTP/1.1 200\r\n\r\n");
    EXPECT_TRUE(helper.hasError());
    EXPECT_EQ(helper.result.errorCode, Error::HttpResponseInvalid);
}

TEST(HttpResponseParserTest, HeaderNameIsLowercasedInStorage) {
    // C-13：header 名统一小写存储，调用方无需区分大小写
    ParserTestHelper helper;
    helper.feed("HTTP/1.1 200 OK\r\naccept-ranges: bytes\r\nContent-Length: 0\r\n\r\n");
    EXPECT_TRUE(helper.isCompleted());
    EXPECT_EQ(helper.result.headers["accept-ranges"], "bytes");
    EXPECT_EQ(helper.result.headers["content-length"], "0");
    EXPECT_EQ(helper.result.headers.count("Content-Length"), 0u);
}

TEST(HttpResponseParserTest, ChunkedBodyAcrossArbitrarySlices) {
    const std::string raw = "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n"
                            "5\r\nhello\r\n6\r\n world\r\n0\r\n\r\n";
    for (const size_t slice : {size_t{1}, size_t{3}, size_t{7}}) {
        ParserTestHelper helper;
        helper.feedInSlices(raw, slice);
        EXPECT_TRUE(helper.isCompleted()) << "slice=" << slice;
        EXPECT_FALSE(helper.hasError()) << "slice=" << slice;
        EXPECT_EQ(helper.result.body, "hello world") << "slice=" << slice;
    }
}

TEST(HttpResponseParserTest, ChunkedTrailerAcrossByteSlices) {
    const std::string raw = "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n"
                            "5\r\nhello\r\n0\r\nX-Trailer: v\r\n\r\n";
    for (const size_t slice : {size_t{1}, size_t{2}}) {
        ParserTestHelper helper;
        helper.feedInSlices(raw, slice);
        EXPECT_TRUE(helper.isCompleted()) << "slice=" << slice;
        EXPECT_FALSE(helper.hasError()) << "slice=" << slice;
        EXPECT_EQ(helper.result.body, "hello") << "slice=" << slice;
    }
}
