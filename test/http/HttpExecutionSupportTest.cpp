#include "HttpExecutionSupport.h"

#include <gtest/gtest.h>

#include <string>
#include <unordered_map>

using namespace slark;
using namespace slark::http;

TEST(HttpExecutionSupportTest, ParserCallbacksCollectResponsePartsAndPerf) {
    ParserEventState state;
    RequestPerf perf;
    HttpResponseParser parser(makeParserCallbacks(state, perf));
    parser.setPerf(&perf);

    constexpr std::string_view response =
        "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n"
        "5\r\nhello\r\n0\r\nX-Trace: unit-test\r\n\r\n";
    parser.feed(reinterpret_cast<const Byte*>(response.data()), response.size());

    ASSERT_TRUE(parser.isCompleted());
    ASSERT_TRUE(state.hasHeaders);
    EXPECT_EQ(state.header.httpStatusCode, HttpStatusCode::OK);
    EXPECT_EQ(state.header.reasonPhrase, "OK");
    ASSERT_NE(state.pendingBody, nullptr);
    EXPECT_EQ(state.pendingBody->toString(), "hello");
    EXPECT_EQ(state.trailers.at("x-trace"), "unit-test");
    EXPECT_EQ(perf.parser.bodySize, 5);
    EXPECT_FALSE(state.parseError);
}

TEST(HttpExecutionSupportTest, ParserCallbacksRecordParseError) {
    ParserEventState state;
    RequestPerf perf;
    HttpResponseParser parser(makeParserCallbacks(state, perf));
    parser.setPerf(&perf);

    constexpr std::string_view response = "invalid response\r\n\r\n";
    parser.feed(reinterpret_cast<const Byte*>(response.data()), response.size());

    EXPECT_TRUE(parser.hasError());
    EXPECT_TRUE(state.parseError);
    EXPECT_EQ(state.parseErrorCode, Error::HttpResponseInvalid);
    EXPECT_EQ(perf.parser.parseResult, Error::HttpResponseInvalid);
}

TEST(HttpExecutionSupportTest, TerminalSettlementMapsHttpAndTransportSuccess) {
    RequestPerf perf;
    HttpResponseParser errorParser({});
    constexpr std::string_view errorResponse =
        "HTTP/1.1 500 Internal Server Error\r\nContent-Length: 0\r\n\r\n";
    errorParser.feed(reinterpret_cast<const Byte*>(errorResponse.data()), errorResponse.size());

    auto errorEvent = makeResponseTerminalEvent(
        errorParser, perf, std::unordered_map<std::string, std::string>{{"x-trace", "error"}});
    EXPECT_EQ(errorEvent.type, HttpResponseEventType::Error);
    EXPECT_EQ(errorEvent.error.retCode, Error::HttpError);
    EXPECT_EQ(errorEvent.error.originErrorCode, 500);
    EXPECT_EQ(errorEvent.trailers.at("x-trace"), "error");

    HttpResponseParser successParser({});
    constexpr std::string_view successResponse =
        "HTTP/1.1 204 No Content\r\nContent-Length: 0\r\n\r\n";
    successParser.feed(reinterpret_cast<const Byte*>(successResponse.data()), successResponse.size());

    auto successEvent = makeResponseTerminalEvent(successParser, {}, {});
    EXPECT_EQ(successEvent.type, HttpResponseEventType::Completed);
    EXPECT_EQ(successEvent.error.retCode, Error::Success);
}

TEST(HttpExecutionSupportTest, StreamReadTimeoutClassificationPreservesSource) {
    RequestPerf perf;

    const auto idle = classifyStreamReadError(Error::ConnectionTimeout, false, perf);
    EXPECT_EQ(idle.retCode, Error::ConnectionTimeout);
    EXPECT_EQ(perf.transport.timeoutSource, TimeoutSource::ReadIdle);

    const auto overall = classifyStreamReadError(Error::ConnectionTimeout, true, perf);
    EXPECT_EQ(overall.retCode, Error::ConnectionTimeout);
    EXPECT_EQ(perf.transport.timeoutSource, TimeoutSource::ReadOverall);

    const auto network = classifyStreamReadError(Error::Failed, false, perf);
    EXPECT_EQ(network.retCode, Error::NetworkError);
}
