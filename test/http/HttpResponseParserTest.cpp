#include "HttpResponseParser.h"

#include <gtest/gtest.h>

#include <array>
#include <memory>
#include <string>
#include <string_view>
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
    size_t statusLineCount = 0;
    size_t headersCount = 0;
    size_t bodyChunkCount = 0;
    size_t completedCount = 0;
    size_t errorCount = 0;
    std::string callbackOrder;
};

class ParserTestHelper {
 public:
    HttpResponseParser::Callbacks callbacks;
    ParseResult result;

    ParserTestHelper() {
        callbacks.onStatusLine = [this](HttpStatusCode code, const std::string& reason) {
            ++result.statusLineCount;
            result.callbackOrder.push_back('S');
            result.statusCode = code;
            result.reasonPhrase = reason;
        };
        callbacks.onHeaders = [this](std::unordered_map<std::string, std::string>&& h) {
            ++result.headersCount;
            result.callbackOrder.push_back('H');
            result.headers = std::move(h);
        };
        callbacks.onBodyData = [this](const Byte* data, size_t size) {
            ++result.bodyChunkCount;
            result.callbackOrder.push_back('B');
            result.body.append(reinterpret_cast<const char*>(data), size);
        };
        callbacks.onCompleted = [this]() {
            ++result.completedCount;
            result.callbackOrder.push_back('C');
            result.completed = true;
        };
        callbacks.onError = [this](Error code) {
            ++result.errorCount;
            result.callbackOrder.push_back('E');
            result.errored = true;
            result.errorCode = code;
        };
        parser = std::make_unique<HttpResponseParser>(callbacks);
    }

    void feed(const std::string& data) {
        parser->feed(reinterpret_cast<const Byte*>(data.data()), data.size());
    }

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
    EXPECT_EQ(helper.result.callbackOrder, "SHBC");
}

TEST(HttpResponseParserTest, ParseChunkedResponse) {
    ParserTestHelper helper;
    helper.feed("HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n");
    EXPECT_FALSE(helper.isCompleted());
    helper.feed("5\r\nHello\r\n0\r\n\r\n");
    EXPECT_TRUE(helper.isCompleted());
    EXPECT_EQ(helper.result.body, "Hello");
}

TEST(HttpResponseParserTest, CompletedParserIgnoresFurtherInput) {
    ParserTestHelper helper;
    helper.feed("HTTP/1.1 200 OK\r\nContent-Length: 2\r\n\r\nOK");
    helper.feed("HTTP/1.1 500 Error\r\nContent-Length: 3\r\n\r\nERR");
    EXPECT_TRUE(helper.isCompleted());
    EXPECT_FALSE(helper.hasError());
    EXPECT_EQ(helper.result.body, "OK");
    EXPECT_EQ(helper.result.statusLineCount, 1u);
    EXPECT_EQ(helper.result.headersCount, 1u);
    EXPECT_EQ(helper.result.completedCount, 1u);
    EXPECT_EQ(helper.result.errorCount, 0u);
}

TEST(HttpResponseParserTest, ErrorParserIgnoresFurtherInput) {
    ParserTestHelper helper;
    helper.feed("HTTP/1.1 200 OK\r\nContent-Length: invalid\r\n\r\n");
    helper.feed("HTTP/1.1 200 OK\r\nContent-Length: 0\r\n\r\n");
    EXPECT_TRUE(helper.hasError());
    EXPECT_EQ(helper.result.completedCount, 0u);
    EXPECT_EQ(helper.result.errorCount, 1u);
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

TEST(HttpResponseParserTest, FixedLengthBodyTruncatedAtEofFails) {
    ParserTestHelper helper;
    helper.feed("HTTP/1.1 200 OK\r\nContent-Length: 5\r\n\r\nhel");
    EXPECT_FALSE(helper.isCompleted());
    helper.rawParser()->onEof();
    EXPECT_TRUE(helper.hasError());
    EXPECT_EQ(helper.result.errorCode, Error::ConnectionReset);
    EXPECT_EQ(helper.result.completedCount, 0u);
    EXPECT_EQ(helper.result.errorCount, 1u);
}

TEST(HttpResponseParserTest, EofBeforeHeadersFails) {
    ParserTestHelper helper;
    helper.feed("HTTP/1.1 200 OK\r\nContent-Length: 5\r\n");
    helper.rawParser()->onEof();
    EXPECT_TRUE(helper.hasError());
    EXPECT_EQ(helper.result.errorCode, Error::ConnectionReset);
    EXPECT_EQ(helper.result.errorCount, 1u);
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

TEST(HttpResponseParserTest, SwitchingProtocolsIsFinalResponse) {
    ParserTestHelper helper;
    helper.feed("HTTP/1.1 101 Switching Protocols\r\n\r\n");
    EXPECT_TRUE(helper.isCompleted());
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
    EXPECT_FALSE(helper.rawParser()->isHttp10());
}

TEST(HttpResponseParserTest, Http10DefaultsToClose) {
    ParserTestHelper helper;
    helper.feed("HTTP/1.0 200 OK\r\nContent-Length: 0\r\n\r\n");
    EXPECT_TRUE(helper.isCompleted());
    EXPECT_TRUE(helper.rawParser()->isHttp10());
    EXPECT_TRUE(helper.rawParser()->shouldCloseConnection());
}

TEST(HttpResponseParserTest, Http10KeepAliveOverridesDefaultClose) {
    ParserTestHelper helper;
    helper.feed("HTTP/1.0 200 OK\r\nConnection: keep-alive\r\n"
                "Content-Length: 0\r\n\r\n");
    EXPECT_TRUE(helper.isCompleted());
    EXPECT_FALSE(helper.rawParser()->shouldCloseConnection());
}

TEST(HttpResponseParserTest, RepeatedConnectionHeadersKeepCloseToken) {
    ParserTestHelper helper;
    helper.feed("HTTP/1.1 200 OK\r\nConnection: keep-alive\r\n"
                "Connection: close\r\nContent-Length: 0\r\n\r\n");
    EXPECT_TRUE(helper.isCompleted());
    EXPECT_TRUE(helper.rawParser()->shouldCloseConnection());
}

TEST(HttpResponseParserTest, UnsupportedHttpVersionFails) {
    for (const auto statusLine : {"HTTP/1.2 200 OK", "HTTP/2.0 200 OK", "XHTTP/1.1 200 OK"}) {
        ParserTestHelper helper;
        helper.feed(std::string(statusLine) + "\r\nContent-Length: 0\r\n\r\n");
        EXPECT_TRUE(helper.hasError()) << statusLine;
        EXPECT_EQ(helper.result.errorCode, Error::HttpResponseInvalid) << statusLine;
    }
}

TEST(HttpResponseParserTest, StatusCodeMustHaveThreeDigits) {
    for (const auto statusLine : {"HTTP/1.1 20 OK", "HTTP/1.1 2000 OK", "HTTP/1.1 abc OK"}) {
        ParserTestHelper helper;
        helper.feed(std::string(statusLine) + "\r\nContent-Length: 0\r\n\r\n");
        EXPECT_TRUE(helper.hasError()) << statusLine;
        EXPECT_EQ(helper.result.errorCode, Error::HttpResponseInvalid) << statusLine;
    }
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

TEST(HttpResponseParserTest, InformationalResponseContinuesToFinalResponse) {
    ParserTestHelper helper;
    helper.feed("HTTP/1.1 100 Continue\r\n\r\n"
                "HTTP/1.1 200 OK\r\nContent-Length: 5\r\n\r\nHello");
    EXPECT_TRUE(helper.isCompleted());
    EXPECT_FALSE(helper.hasError());
    EXPECT_EQ(helper.result.statusCode, HttpStatusCode::OK);
    EXPECT_EQ(helper.result.body, "Hello");
    EXPECT_EQ(helper.result.statusLineCount, 2u);
    EXPECT_EQ(helper.result.headersCount, 1u);
    EXPECT_EQ(helper.result.completedCount, 1u);
    EXPECT_EQ(helper.result.errorCount, 0u);
    EXPECT_EQ(helper.result.callbackOrder, "SSHBC");
}

TEST(HttpResponseParserTest, MultipleInformationalResponsesAreSkipped) {
    ParserTestHelper helper;
    helper.feed("HTTP/1.1 100 Continue\r\nX-Info: one\r\n\r\n"
                "HTTP/1.1 102 Processing\r\nX-Info: two\r\n\r\n"
                "HTTP/1.1 103 Early Hints\r\nLink: </style.css>\r\n\r\n"
                "HTTP/1.1 200 OK\r\nContent-Length: 2\r\n\r\nOK");
    EXPECT_TRUE(helper.isCompleted());
    EXPECT_FALSE(helper.hasError());
    EXPECT_EQ(helper.result.statusCode, HttpStatusCode::OK);
    EXPECT_EQ(helper.result.body, "OK");
    EXPECT_EQ(helper.result.statusLineCount, 4u);
    EXPECT_EQ(helper.result.headersCount, 1u);
    EXPECT_EQ(helper.result.completedCount, 1u);
    EXPECT_EQ(helper.result.errorCount, 0u);
    EXPECT_EQ(helper.result.callbackOrder, "SSSSHBC");
}

TEST(HttpResponseParserTest, InformationalFramingHeadersDoNotCreateBody) {
    ParserTestHelper helper;
    helper.feed("HTTP/1.1 100 Continue\r\nContent-Length: 5\r\n\r\n"
                "HTTP/1.1 200 OK\r\nContent-Length: 2\r\n\r\nOK");
    EXPECT_TRUE(helper.isCompleted());
    EXPECT_FALSE(helper.hasError());
    EXPECT_EQ(helper.result.body, "OK");
    EXPECT_EQ(helper.result.headersCount, 1u);
    EXPECT_EQ(helper.result.bodyChunkCount, 1u);

    ParserTestHelper chunkedHelper;
    chunkedHelper.feed("HTTP/1.1 102 Processing\r\nTransfer-Encoding: chunked\r\n\r\n"
                       "HTTP/1.1 200 OK\r\nContent-Length: 2\r\n\r\nOK");
    EXPECT_TRUE(chunkedHelper.isCompleted());
    EXPECT_FALSE(chunkedHelper.hasError());
    EXPECT_EQ(chunkedHelper.result.body, "OK");
    EXPECT_EQ(chunkedHelper.result.headersCount, 1u);
}

TEST(HttpResponseParserTest, InformationalResponsesWorkAcrossByteSlices) {
    const std::string raw = "HTTP/1.1 102 Processing\r\n\r\n"
                            "HTTP/1.1 103 Early Hints\r\n\r\n"
                            "HTTP/1.1 200 OK\r\nContent-Length: 2\r\n\r\nOK";
    for (const size_t slice : {size_t{1}, size_t{2}, size_t{7}}) {
        ParserTestHelper helper;
        helper.feedInSlices(raw, slice);
        EXPECT_TRUE(helper.isCompleted()) << "slice=" << slice;
        EXPECT_FALSE(helper.hasError()) << "slice=" << slice;
        EXPECT_EQ(helper.result.statusLineCount, 3u) << "slice=" << slice;
        EXPECT_EQ(helper.result.headersCount, 1u) << "slice=" << slice;
        EXPECT_EQ(helper.result.completedCount, 1u) << "slice=" << slice;
    }
}

TEST(HttpResponseParserTest, SwitchingProtocolsDoesNotParseFollowingResponse) {
    ParserTestHelper helper;
    helper.feed("HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\n\r\n"
                "HTTP/1.1 200 OK\r\nContent-Length: 2\r\n\r\nOK");
    EXPECT_TRUE(helper.isCompleted());
    EXPECT_FALSE(helper.hasError());
    EXPECT_EQ(helper.result.statusLineCount, 1u);
    EXPECT_EQ(helper.result.headersCount, 1u);
    EXPECT_EQ(helper.result.completedCount, 1u);
    EXPECT_EQ(helper.result.bodyChunkCount, 0u);
    EXPECT_EQ(helper.result.callbackOrder, "SHC");
}

TEST(HttpResponseParserTest, NoBodyResponsesIgnoreFramingHeaders) {
    const std::array<std::string_view, 4> statusLines = {
        "HTTP/1.1 204 No Content", "HTTP/1.1 205 Reset Content",
        "HTTP/1.1 304 Not Modified", "HTTP/1.1 200 OK"};
    const std::array<std::string_view, 2> framingHeaders = {
        "Content-Length: 5\r\n", "Transfer-Encoding: chunked\r\n"};

    for (const auto statusLine : statusLines) {
        for (const auto framingHeader : framingHeaders) {
            ParserTestHelper helper;
            const auto response = std::string(statusLine) + "\r\n" + std::string(framingHeader) +
                                   "\r\nunexpected";
            if (statusLine == "HTTP/1.1 200 OK") {
                helper.rawParser()->setRequestMethod(HttpMethodType::Head);
            }
            helper.feed(response);
            EXPECT_TRUE(helper.isCompleted()) << statusLine << " / " << framingHeader;
            EXPECT_FALSE(helper.hasError()) << statusLine << " / " << framingHeader;
            EXPECT_TRUE(helper.result.body.empty()) << statusLine << " / " << framingHeader;
            EXPECT_EQ(helper.result.bodyChunkCount, 0u) << statusLine << " / " << framingHeader;
            EXPECT_EQ(helper.result.headersCount, 1u) << statusLine << " / " << framingHeader;
            EXPECT_EQ(helper.result.completedCount, 1u) << statusLine << " / " << framingHeader;
            EXPECT_EQ(helper.result.callbackOrder, "SHC") << statusLine << " / " << framingHeader;
        }
    }
}

TEST(HttpResponseParserTest, HeadResponseWithChunkedFramingHasNoBody) {
    ParserTestHelper helper;
    helper.rawParser()->setRequestMethod(HttpMethodType::Head);
    helper.feed("HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n"
                "5\r\nHello\r\n0\r\n\r\n");
    EXPECT_TRUE(helper.isCompleted());
    EXPECT_FALSE(helper.hasError());
    EXPECT_TRUE(helper.result.body.empty());
    EXPECT_EQ(helper.result.bodyChunkCount, 0u);
    EXPECT_EQ(helper.result.callbackOrder, "SHC");
}

TEST(HttpResponseParserTest, ResetContentHasNoBody) {
    ParserTestHelper helper;
    helper.feed("HTTP/1.1 205 Reset Content\r\nContent-Length: 5\r\n\r\n");
    EXPECT_TRUE(helper.isCompleted());
    EXPECT_TRUE(helper.result.body.empty());
}

TEST(HttpResponseParserTest, SameDuplicateContentLengthIsAccepted) {
    ParserTestHelper helper;
    helper.feed("HTTP/1.1 200 OK\r\nContent-Length: 3\r\nContent-Length: 3\r\n\r\nabc");
    EXPECT_TRUE(helper.isCompleted());
    EXPECT_EQ(helper.result.body, "abc");
}

TEST(HttpResponseParserTest, ConflictingContentLengthFails) {
    ParserTestHelper helper;
    helper.feed("HTTP/1.1 200 OK\r\nContent-Length: 5\r\nContent-Length: 3\r\n\r\nabc");
    EXPECT_TRUE(helper.hasError());
    EXPECT_EQ(helper.result.errorCode, Error::HttpResponseInvalid);
}

TEST(HttpResponseParserTest, FixedLengthBodyLimitRejectsBeforeBodyRead) {
    ParserTestHelper helper;
    helper.rawParser()->setMaxResponseBodySize(4);
    helper.feed("HTTP/1.1 200 OK\r\nContent-Length: 5\r\n\r\nHello");
    EXPECT_TRUE(helper.hasError());
    EXPECT_EQ(helper.result.errorCode, Error::HttpResponseBodyTooLarge);
    EXPECT_TRUE(helper.result.body.empty());
    EXPECT_EQ(helper.result.errorCount, 1u);
    EXPECT_EQ(helper.result.completedCount, 0u);
}

TEST(HttpResponseParserTest, ChunkedBodyLimitRejectsBeforeExceedingChunk) {
    ParserTestHelper helper;
    helper.rawParser()->setMaxResponseBodySize(4);
    helper.feed("HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n"
                "3\r\nabc\r\n2\r\nde\r\n0\r\n\r\n");
    EXPECT_TRUE(helper.hasError());
    EXPECT_EQ(helper.result.errorCode, Error::HttpResponseBodyTooLarge);
    EXPECT_EQ(helper.result.body, "abc");
    EXPECT_EQ(helper.result.completedCount, 0u);
}

TEST(HttpResponseParserTest, UnknownLengthBodyLimitRejectsAccumulatedData) {
    ParserTestHelper helper;
    helper.rawParser()->setMaxResponseBodySize(4);
    helper.feed("HTTP/1.1 200 OK\r\n\r\n12345");
    EXPECT_TRUE(helper.hasError());
    EXPECT_EQ(helper.result.errorCode, Error::HttpResponseBodyTooLarge);
    EXPECT_TRUE(helper.result.body.empty());
}

TEST(HttpResponseParserTest, ZeroBodyLimitAllowsEmptyResponse) {
    ParserTestHelper helper;
    helper.rawParser()->setMaxResponseBodySize(0);
    helper.feed("HTTP/1.1 204 No Content\r\nContent-Length: 5\r\n\r\n");
    EXPECT_TRUE(helper.isCompleted());
    EXPECT_FALSE(helper.hasError());
    EXPECT_EQ(helper.result.completedCount, 1u);
}

TEST(HttpResponseParserTest, InvalidContentLengthTrailingCharsFails) {
    ParserTestHelper helper;
    helper.feed("HTTP/1.1 200 OK\r\nContent-Length: 5abc\r\n\r\n");
    EXPECT_TRUE(helper.hasError());
    EXPECT_EQ(helper.result.errorCode, Error::HttpResponseInvalid);
}

TEST(HttpResponseParserTest, InvalidContentLengthValuesFail) {
    for (const auto value : {"", "-1", "+1", "0x1", "1abc",
                             "9223372036854775808"}) {
        ParserTestHelper helper;
        helper.feed(std::string("HTTP/1.1 200 OK\r\nContent-Length: ") + value + "\r\n\r\n");
        EXPECT_TRUE(helper.hasError()) << "value=" << value;
        EXPECT_EQ(helper.result.errorCode, Error::HttpResponseInvalid) << "value=" << value;
        EXPECT_EQ(helper.result.completedCount, 0u) << "value=" << value;
        EXPECT_EQ(helper.result.errorCount, 1u) << "value=" << value;
    }
}

TEST(HttpResponseParserTest, ContentLengthAllowsOptionalWhitespace) {
    ParserTestHelper helper;
    helper.feed("HTTP/1.1 200 OK\r\nContent-Length: \t5 \t\r\n\r\nHello");
    EXPECT_TRUE(helper.isCompleted());
    EXPECT_FALSE(helper.hasError());
    EXPECT_EQ(helper.result.body, "Hello");
}

TEST(HttpResponseParserTest, SameDuplicateContentLengthWithDifferentCaseIsAccepted) {
    ParserTestHelper helper;
    helper.feed("HTTP/1.1 200 OK\r\nContent-Length: 3\r\ncontent-length: 3\r\n\r\nabc");
    EXPECT_TRUE(helper.isCompleted());
    EXPECT_FALSE(helper.hasError());
    EXPECT_EQ(helper.result.body, "abc");
}

TEST(HttpResponseParserTest, HeaderLineWithoutColonFails) {
    ParserTestHelper helper;
    helper.feed("HTTP/1.1 200 OK\r\nBadHeaderNoColon\r\nContent-Length: 0\r\n\r\n");
    EXPECT_TRUE(helper.hasError());
    EXPECT_EQ(helper.result.errorCode, Error::HttpResponseInvalid);
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

TEST(HttpResponseParserTest, NegativeChunkSizeFails) {
    ParserTestHelper helper;
    helper.feed("HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n-1\r\n");
    EXPECT_TRUE(helper.hasError());
    EXPECT_EQ(helper.result.errorCode, Error::HttpChunkedError);
}

TEST(HttpResponseParserTest, TransferEncodingAndContentLengthConflictFails) {
    for (const auto& headers : {"Transfer-Encoding: chunked\r\nContent-Length: 5\r\n",
                                "Content-Length: 5\r\nTransfer-Encoding: chunked\r\n"}) {
        ParserTestHelper helper;
        helper.feed(std::string("HTTP/1.1 200 OK\r\n") + headers + "\r\n");
        EXPECT_TRUE(helper.hasError()) << headers;
        EXPECT_EQ(helper.result.errorCode, Error::HttpResponseInvalid) << headers;
    }
}

TEST(HttpResponseParserTest, UnsupportedTransferEncodingFails) {
    for (const auto encoding : {"chunkedness", "gzip", "gzip, chunked"}) {
        ParserTestHelper helper;
        helper.feed(std::string("HTTP/1.1 200 OK\r\nTransfer-Encoding: ") + encoding +
                    "\r\n\r\n");
        EXPECT_TRUE(helper.hasError()) << "encoding=" << encoding;
        EXPECT_EQ(helper.result.errorCode, Error::HttpResponseInvalid) << "encoding=" << encoding;
    }
}

TEST(HttpResponseParserTest, TransferEncodingChunkedIsCaseInsensitive) {
    ParserTestHelper helper;
    helper.feed("HTTP/1.1 200 OK\r\nTransfer-Encoding: \tChunked \t\r\n\r\n"
                "0\r\n\r\n");
    EXPECT_TRUE(helper.isCompleted());
    EXPECT_FALSE(helper.hasError());
    EXPECT_EQ(helper.result.completedCount, 1u);
}

TEST(HttpResponseParserTest, RepeatedTransferEncodingFails) {
    for (const auto headers : {"Transfer-Encoding: chunked\r\nTransfer-Encoding: chunked\r\n",
                               "Transfer-Encoding: gzip, chunked\r\n",
                               "Transfer-Encoding: chunked, gzip\r\n",
                               "Transfer-Encoding: chunked, chunked\r\n"}) {
        ParserTestHelper helper;
        helper.feed(std::string("HTTP/1.1 200 OK\r\n") + headers + "\r\n");
        EXPECT_TRUE(helper.hasError()) << headers;
        EXPECT_EQ(helper.result.errorCode, Error::HttpResponseInvalid) << headers;
    }
}

TEST(HttpResponseParserTest, ConnectionCloseTokenIsRecognized) {
    for (const auto value : {"keep-alive, close", "close, keep-alive"}) {
        ParserTestHelper helper;
        helper.feed(std::string("HTTP/1.1 200 OK\r\nConnection: ") + value +
                    "\r\nContent-Length: 0\r\n\r\n");
        EXPECT_TRUE(helper.isCompleted()) << value;
        EXPECT_TRUE(helper.rawParser()->shouldCloseConnection()) << value;
    }
}

TEST(HttpResponseParserTest, InvalidChunkSizeValuesFail) {
    for (const auto value : {"", "+1", "-1", "0x1", "g", "1g"}) {
        ParserTestHelper helper;
        helper.feed("HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n");
        helper.feed(std::string(value) + "\r\n");
        helper.rawParser()->onEof();
        EXPECT_TRUE(helper.hasError()) << "value=" << value;
        EXPECT_EQ(helper.result.errorCode, Error::HttpChunkedError) << "value=" << value;
        EXPECT_EQ(helper.result.errorCount, 1u) << "value=" << value;
    }
}

TEST(HttpResponseParserTest, ChunkExtensionIsAccepted) {
    ParserTestHelper helper;
    helper.feed("HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n"
                "5;foo=bar\r\nHello\r\n0\r\n\r\n");
    EXPECT_TRUE(helper.isCompleted());
    EXPECT_FALSE(helper.hasError());
    EXPECT_EQ(helper.result.body, "Hello");
}

TEST(HttpResponseParserTest, ChunkDataMissingAtEofFails) {
    ParserTestHelper helper;
    helper.feed("HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n"
                "5\r\nhel");
    EXPECT_FALSE(helper.isCompleted());
    helper.rawParser()->onEof();
    EXPECT_TRUE(helper.hasError());
    EXPECT_EQ(helper.result.errorCode, Error::ConnectionReset);
}

TEST(HttpResponseParserTest, ChunkDataWithoutTrailingCrlfFailsAtEof) {
    ParserTestHelper helper;
    helper.feed("HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n"
                "5\r\nHello");
    EXPECT_FALSE(helper.isCompleted());
    helper.rawParser()->onEof();
    EXPECT_TRUE(helper.hasError());
    EXPECT_EQ(helper.result.errorCode, Error::ConnectionReset);
}

TEST(HttpResponseParserTest, MalformedTrailerFails) {
    ParserTestHelper helper;
    helper.feed("HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n"
                "0\r\nMalformedTrailer\r\n\r\n");
    EXPECT_TRUE(helper.hasError());
    EXPECT_EQ(helper.result.errorCode, Error::HttpResponseInvalid);
}

TEST(HttpResponseParserTest, MultipleTrailersAreAccepted) {
    ParserTestHelper helper;
    helper.feed("HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n"
                "0\r\nX-One: one\r\nX-Two: two\r\n\r\n");
    EXPECT_TRUE(helper.isCompleted());
    EXPECT_FALSE(helper.hasError());
}

TEST(HttpResponseParserTest, TrailerSizeLimitIsEnforced) {
    ParserTestHelper helper;
    std::string response = "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n0\r\n";
    response.append(16 * 1024, 'a');
    response.append("\r\n\r\n");
    helper.feed(response);
    EXPECT_TRUE(helper.hasError());
    EXPECT_EQ(helper.result.errorCode, Error::HttpResponseInvalid);
}

TEST(HttpResponseParserTest, StatusLineWithoutReasonPhraseFails) {
    ParserTestHelper helper;
    helper.feed("HTTP/1.1 200\r\n\r\n");
    EXPECT_TRUE(helper.hasError());
    EXPECT_EQ(helper.result.errorCode, Error::HttpResponseInvalid);
}

TEST(HttpResponseParserTest, HeaderNameIsLowercasedInStorage) {
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
