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
    EXPECT_EQ(parser.trailers().at("x-trace"), "unit-test");
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

TEST(HttpExecutionSupportTest, StreamChunkCarriesRequestPerfOnTerminal) {
    RequestPerf perf;
    perf.transport.redirectCount = 2;
    perf.transport.recvLoops = 7;
    perf.transport.totalSendTimeMs = 3;
    perf.transport.totalReceiveTimeMs = 11;
    perf.totalRequestTimeMs = 20;

    const auto final = StreamChunk::makeFinal({}, perf);
    EXPECT_TRUE(final.isFinal);
    EXPECT_EQ(final.perf.transport.redirectCount, 2);
    EXPECT_EQ(final.perf.transport.recvLoops, 7);
    EXPECT_EQ(final.perf.totalRequestTimeMs, 20);

    const auto error = StreamChunk::makeError(ErrorInfo{Error::ConnectionReset}, perf);
    EXPECT_TRUE(error.hasError());
    EXPECT_EQ(error.perf.transport.totalSendTimeMs, 3);
    EXPECT_EQ(error.perf.transport.totalReceiveTimeMs, 11);
}

TEST(HttpExecutionSupportTest, PerfTerminalContractDistinguishesCapturedMetrics) {
    RequestPerf captured;
    captured.requestStartUs = steadyUs();
    captured.transport.redirectCount = 4;
    const auto final = StreamChunk::makeFinal({}, captured);
    EXPECT_TRUE(final.hasPerf);
    EXPECT_TRUE(final.perf.hasPerf());
    EXPECT_EQ(final.perf.transport.redirectCount, 4);

    const auto synthesized = StreamChunk::makeError(ErrorInfo{Error::ConnectionReset});
    EXPECT_FALSE(synthesized.hasPerf);
    EXPECT_FALSE(synthesized.perf.hasPerf());
}

TEST(HttpExecutionSupportTest, TerminalEventsFinalizeElapsedTime) {
    RequestPerf perf;
    perf.requestStartUs = steadyUs() - 5000;
    const auto error = HttpResponseEvent::makeError(ErrorInfo{Error::Cancelled}, perf);
    EXPECT_EQ(error.type, HttpResponseEventType::Error);
    EXPECT_GE(error.perf.totalRequestTimeMs, 5);

    RequestPerf completed;
    completed.requestStartUs = steadyUs() - 3000;
    const auto done = HttpResponseEvent::makeCompleted(completed);
    EXPECT_EQ(done.type, HttpResponseEventType::Completed);
    EXPECT_GE(done.perf.totalRequestTimeMs, 3);
}

TEST(HttpExecutionSupportTest, ParserTerminalErrorPreservesSpecificCodesAndResets) {
    HttpResponseParser completedParser({});
    constexpr std::string_view ok = "HTTP/1.1 200 OK\r\nContent-Length: 0\r\n\r\n";
    completedParser.feed(reinterpret_cast<const Byte*>(ok.data()), ok.size());
    ParserEventState cleanState;
    EXPECT_EQ(parserTerminalError(completedParser, cleanState).retCode, Error::Success);

    HttpResponseParser truncated({});
    constexpr std::string_view partial = "HTTP/1.1 200 OK\r\nContent-Length: 20\r\n\r\nabc";
    truncated.feed(reinterpret_cast<const Byte*>(partial.data()), partial.size());
    EXPECT_EQ(parserTerminalError(truncated, cleanState).retCode, Error::ConnectionReset);

    ParserEventState failedState;
    failedState.parseError = true;
    failedState.parseErrorCode = Error::HttpResponseInvalid;
    EXPECT_EQ(parserTerminalError(truncated, failedState).retCode, Error::HttpResponseInvalid);
}

TEST(HttpExecutionSupportTest, TimeoutSourceNamesAreReadable) {
    EXPECT_EQ(timeoutSourceName(TimeoutSource::None), "none");
    EXPECT_EQ(timeoutSourceName(TimeoutSource::Connect), "connect");
    EXPECT_EQ(timeoutSourceName(TimeoutSource::TlsHandshake), "tls-handshake");
    EXPECT_EQ(timeoutSourceName(TimeoutSource::Write), "write");
    EXPECT_EQ(timeoutSourceName(TimeoutSource::ReadIdle), "read-idle");
    EXPECT_EQ(timeoutSourceName(TimeoutSource::ReadOverall), "read-overall");
}

TEST(HttpExecutionSupportTest, BodyChunksAreCountedForFixedLengthResponses) {
    ParserEventState state;
    RequestPerf perf;
    HttpResponseParser parser(makeParserCallbacks(state, perf));
    parser.setPerf(&perf);

    constexpr std::string_view response =
        "HTTP/1.1 200 OK\r\nContent-Length: 5\r\n\r\nhello";
    parser.feed(reinterpret_cast<const Byte*>(response.data()), response.size());

    ASSERT_TRUE(parser.isCompleted());
    EXPECT_EQ(perf.parser.bodySize, 5);
    EXPECT_EQ(perf.parser.chunkCount, 1);
}

TEST(HttpExecutionSupportTest, TerminalEventsPublishNetworkSamplesToSink) {
    slark::perf::NetworkStats received;
    slark::perf::setNetworkSampleSink(
        [&received](const slark::perf::NetworkSample& sample) { received.record(sample); });

    RequestPerf perf;
    perf.requestStartUs = steadyUs();
    perf.host = "example.com";
    perf.connection.connectTimeMs = 12;
    perf.totalRequestTimeMs = 34;
    perf.transport.totalSendTimeMs = 3;
    perf.transport.totalReceiveTimeMs = 9;
    perf.transport.redirectCount = 2;
    perf.parser.bodySize = 1024;
    perf.connection.reused = true;
    perf.perfSessionId = "sess-42";

    HttpResponseEvent::makeCompleted(perf);
    slark::perf::setNetworkSampleSink({});

    ASSERT_EQ(received.requestCount, 1u);
    EXPECT_EQ(received.successCount, 1u);
    EXPECT_EQ(received.totalBodyBytes, 1024u);
    EXPECT_EQ(received.lastSample.host, "example.com");
    EXPECT_EQ(received.lastSample.perfSessionId, "sess-42");
    EXPECT_EQ(received.lastSample.connectMs, 12);
    EXPECT_EQ(received.lastSample.totalRequestMs, 34);
    EXPECT_EQ(received.lastSample.sendMs, 3);
    EXPECT_EQ(received.lastSample.receiveMs, 9);
    EXPECT_EQ(received.lastSample.redirectCount, 2);
    EXPECT_TRUE(received.lastSample.success);
    EXPECT_TRUE(received.lastSample.connectionReused);
}

TEST(HttpExecutionSupportTest, ErrorTerminalPublishesFailureAndEventsWithoutPerfAreIgnored) {
    slark::perf::NetworkStats received;
    slark::perf::setNetworkSampleSink(
        [&received](const slark::perf::NetworkSample& sample) { received.record(sample); });

    RequestPerf perf;
    perf.requestStartUs = steadyUs();
    perf.transport.timeoutSource = TimeoutSource::ReadIdle;

    HttpResponseEvent::makeError(ErrorInfo{Error::Cancelled}, perf);
    HttpResponseEvent::makeError(ErrorInfo{Error::ConnectionReset});
    slark::perf::setNetworkSampleSink({});

    ASSERT_EQ(received.requestCount, 1u);
    EXPECT_EQ(received.failureCount, 1u);
    EXPECT_FALSE(received.lastSample.success);
    EXPECT_EQ(received.lastSample.timeoutSource, "read-idle");
}
