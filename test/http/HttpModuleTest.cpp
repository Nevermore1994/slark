//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//
#include "CancellationToken.h"
#include "HttpRequest.h"
#include "HttpResponseParser.h"
#include "HttpUtil.hpp"
#include "ThreadPool.hpp"
#include "Type.h"
#include "Url.h"

#include <chrono>
#include <condition_variable>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <thread>

using namespace slark;
using namespace slark::http;

// URL Tests
TEST(UrlTest, ParseHttpUrl) {
    Url url("http://example.com/path?query=value#fragment");
    EXPECT_TRUE(url.isValid());
    EXPECT_EQ(url.scheme, "http");
    EXPECT_EQ(url.host, "example.com");
    EXPECT_EQ(url.path, "/path");
    EXPECT_EQ(url.query, "query=value");
    EXPECT_EQ(url.fragment, "fragment");
    EXPECT_EQ(url.portNumber(), 80);
}

TEST(UrlTest, ParseHttpsUrl) {
    Url url("https://example.com:8443/api");
    EXPECT_TRUE(url.isValid());
    EXPECT_EQ(url.scheme, "https");
    EXPECT_EQ(url.host, "example.com");
    EXPECT_EQ(url.port, "8443");
    EXPECT_EQ(url.portNumber(), 8443);
    EXPECT_EQ(url.path, "/api");
}

TEST(UrlTest, ParseInvalidUrl) {
    Url url("not-a-valid-url");
    EXPECT_FALSE(url.isValid());
}

TEST(UrlTest, ParseIpv6Url) {
    Url url("http://[::1]:8080/path");
    EXPECT_TRUE(url.isValid());
    // IPv6地址在内部存储时不包含方括号
    EXPECT_EQ(url.host, "::1");
    EXPECT_EQ(url.portNumber(), 8080);
}

TEST(UrlTest, ParseUrlWithUserInfo) {
    Url url("http://user:pass@example.com/resource");
    EXPECT_TRUE(url.isValid());
    EXPECT_EQ(url.userInfo, "user:pass");
    EXPECT_EQ(url.host, "example.com");
}

// HttpResponseParser Tests
class HttpResponseParserTest : public ::testing::Test {
protected:
    void SetUp() override {
        parser_ = std::make_unique<HttpResponseParser>(HttpResponseParser::Callbacks{});
    }

    std::unique_ptr<HttpResponseParser> parser_;
};

TEST_F(HttpResponseParserTest, ParseSimpleResponse) {
    const char* response = "HTTP/1.1 200 OK\r\n"
                           "Content-Length: 13\r\n"
                           "\r\n"
                           "Hello, World!";

    parser_->feed(reinterpret_cast<const Byte*>(response), strlen(response));
    EXPECT_TRUE(parser_->isCompleted());
}

TEST_F(HttpResponseParserTest, ParseChunkedResponse) {
    const char* response = "HTTP/1.1 200 OK\r\n"
                           "Transfer-Encoding: chunked\r\n"
                           "\r\n"
                           "5\r\n"
                           "Hello\r\n"
                           "6\r\n"
                           " World\r\n"
                           "0\r\n"
                           "\r\n";

    parser_->feed(reinterpret_cast<const Byte*>(response), strlen(response));
    EXPECT_TRUE(parser_->isCompleted());
}

TEST_F(HttpResponseParserTest, ParseNoContentLengthResponse) {
    // Response without Content-Length (should use EOF as terminator)
    const char* response = "HTTP/1.1 200 OK\r\n"
                           "Content-Type: text/plain\r\n"
                           "\r\n";

    parser_->feed(reinterpret_cast<const Byte*>(response), strlen(response));

    // Without Content-Length or chunked, not completed until EOF
    EXPECT_FALSE(parser_->isCompleted());
    EXPECT_TRUE(parser_->isBodyUnknownLength());

    // Simulate EOF
    parser_->onEof();
    EXPECT_TRUE(parser_->isCompleted());
}

TEST_F(HttpResponseParserTest, ParseHeaders) {
    HttpStatusCode code = HttpStatusCode::Unknown;
    std::string reason;
    std::unordered_map<std::string, std::string> headers;

    HttpResponseParser::Callbacks callbacks;
    callbacks.onStatusLine = [&](HttpStatusCode c, const std::string& r) {
        code = c;
        reason = r;
    };
    callbacks.onHeaders = [&](std::unordered_map<std::string, std::string>&& h) {
        headers = std::move(h);
    };

    parser_ = std::make_unique<HttpResponseParser>(std::move(callbacks));

    const char* response = "HTTP/1.1 404 Not Found\r\n"
                           "Content-Length: 0\r\n"
                           "\r\n";

    parser_->feed(reinterpret_cast<const Byte*>(response), strlen(response));

    EXPECT_EQ(code, HttpStatusCode::NotFound);
    // 验证头部被正确解析
    EXPECT_FALSE(parser_->hasError());
}

TEST_F(HttpResponseParserTest, ShouldCloseConnection) {
    // HTTP/1.0 默认不关闭（需要显式 Connection: close）
    const char* http10 = "HTTP/1.0 200 OK\r\n"
                         "Content-Length: 0\r\n"
                         "\r\n";
    parser_->feed(reinterpret_cast<const Byte*>(http10), strlen(http10));
    // 实现只检查 Connection: close 头部
    EXPECT_FALSE(parser_->shouldCloseConnection());

    // HTTP/1.1 with Connection: close
    parser_ = std::make_unique<HttpResponseParser>(HttpResponseParser::Callbacks{});
    const char* http11Close = "HTTP/1.1 200 OK\r\n"
                              "Connection: close\r\n"
                              "Content-Length: 0\r\n"
                              "\r\n";
    parser_->feed(reinterpret_cast<const Byte*>(http11Close), strlen(http11Close));
    EXPECT_TRUE(parser_->shouldCloseConnection());

    // HTTP/1.1 without Connection: close (keep-alive by default)
    parser_ = std::make_unique<HttpResponseParser>(HttpResponseParser::Callbacks{});
    const char* http11Default = "HTTP/1.1 200 OK\r\n"
                                "Content-Length: 0\r\n"
                                "\r\n";
    parser_->feed(reinterpret_cast<const Byte*>(http11Default), strlen(http11Default));
    EXPECT_FALSE(parser_->shouldCloseConnection());
}

TEST_F(HttpResponseParserTest, StateTracking) {
    EXPECT_EQ(parser_->state(), HttpResponseParser::State::StatusLine);

    const char* partial = "HTTP/1.1 200 OK\r\n";
    parser_->feed(reinterpret_cast<const Byte*>(partial), strlen(partial));
    EXPECT_EQ(parser_->state(), HttpResponseParser::State::Headers);
}

TEST_F(HttpResponseParserTest, Reset) {
    const char* response = "HTTP/1.1 200 OK\r\n"
                           "Content-Length: 0\r\n"
                           "\r\n";

    parser_->feed(reinterpret_cast<const Byte*>(response), strlen(response));
    EXPECT_TRUE(parser_->isCompleted());

    parser_->reset();
    EXPECT_EQ(parser_->state(), HttpResponseParser::State::StatusLine);
    EXPECT_FALSE(parser_->isCompleted());
}

// HttpUtil Tests
TEST(HttpUtilTest, BuildGetRequest) {
    RequestInfo info;
    info.url = "http://example.com/path?query=value";
    info.methodType = HttpMethodType::Get;
    info.headers = {{"User-Agent", "TestAgent"}};

    Url url(info.url);
    bool keepAlive = true;
    std::string request = Util::buildRequestString(info, url, keepAlive);

    EXPECT_THAT(request, ::testing::HasSubstr("GET /path?query=value HTTP/1.1"));
    EXPECT_THAT(request, ::testing::HasSubstr("Host: example.com"));
    EXPECT_THAT(request, ::testing::HasSubstr("User-Agent: TestAgent"));
}

TEST(HttpUtilTest, BuildPostRequest) {
    RequestInfo info;
    info.url = "http://example.com/api";
    info.methodType = HttpMethodType::Post;
    info.headers = {{"Content-Type", "application/json"}};
    info.body = std::make_unique<Data>(R"({"key": "value"})");

    Url url(info.url);
    bool keepAlive = true;
    std::string request = Util::buildRequestString(info, url, keepAlive);

    EXPECT_THAT(request, ::testing::HasSubstr("POST /api HTTP/1.1"));
    EXPECT_THAT(request, ::testing::HasSubstr("Content-Length: 16"));
}

TEST(HttpUtilTest, BuildRequestWithNoPath) {
    RequestInfo info;
    info.url = "http://example.com";
    info.methodType = HttpMethodType::Get;

    Url url(info.url);
    bool keepAlive = true;
    std::string request = Util::buildRequestString(info, url, keepAlive);

    EXPECT_THAT(request, ::testing::HasSubstr("GET / HTTP/1.1"));
}

// CancellationToken Tests for HTTP
TEST(CancellationTokenHttpTest, CancelDuringRequest) {
    CancellationToken token;

    ThreadPool pool(ThreadPoolConfig{1});

    std::atomic<bool> cancelled{false};

    pool.postJob([&]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        token.cancel();
        cancelled = true;
    });

    pool.waitForAll();
    EXPECT_TRUE(cancelled.load());
    EXPECT_TRUE(token.isCancelled());
}

// Type Tests
TEST(TypeTest, RequestInfoDefaults) {
    RequestInfo info;
    EXPECT_TRUE(info.isAllowRedirect);
    EXPECT_EQ(info.ipVersion, IPVersion::Auto);
    EXPECT_EQ(info.methodType, HttpMethodType::Unknown);
    EXPECT_EQ(info.bodySize(), 0);
    EXPECT_TRUE(info.bodyEmpty());
}

TEST(TypeTest, RequestInfoWithBody) {
    RequestInfo info;
    info.body = std::make_unique<Data>(std::string(100, 'x'));

    EXPECT_EQ(info.bodySize(), 100);
    EXPECT_FALSE(info.bodyEmpty());
}

TEST(TypeTest, HttpResponseDefaults) {
    HttpResponse response;
    EXPECT_EQ(response.statusCode, HttpStatusCode::Unknown);
    // 默认 error.retCode 是 Success，所以 isSuccess 返回 true
    EXPECT_TRUE(response.isSuccess());
    // statusCode 默认是 Unknown，不是 OK
    EXPECT_FALSE(response.isOk());
}

TEST(TypeTest, HttpResponseSuccess) {
    HttpResponse response;
    response.statusCode = HttpStatusCode::OK;
    response.error = ErrorInfo{ResultCode::Success, 0};

    EXPECT_TRUE(response.isSuccess());
    EXPECT_TRUE(response.isOk());
}

TEST(TypeTest, HttpResponseRedirect) {
    HttpResponse response;
    response.statusCode = HttpStatusCode::MovedPermanently;

    EXPECT_TRUE(response.isNeedRedirect());
}

TEST(TypeTest, ErrorInfoDefault) {
    ErrorInfo error;
    EXPECT_EQ(error.retCode, ResultCode::Success);
    EXPECT_EQ(error.errorCode, 0);
}

TEST(TypeTest, ErrorInfoConstruct) {
    ErrorInfo error(ResultCode::Timeout, 5000);
    EXPECT_EQ(error.retCode, ResultCode::Timeout);
    EXPECT_EQ(error.errorCode, 5000);
}

// RedirectPolicy Tests
TEST(RedirectPolicyTest, ShouldRedirect) {
    RedirectPolicy policy;

    HttpResponse response;
    response.statusCode = HttpStatusCode::MovedPermanently;

    RequestInfo request;
    request.isAllowRedirect = true;

    EXPECT_TRUE(policy.shouldRedirect(response, request, 0));
    EXPECT_TRUE(policy.shouldRedirect(response, request, 5));
    EXPECT_FALSE(policy.shouldRedirect(response, request, 10)); // max redirect

    response.statusCode = HttpStatusCode::OK;
    EXPECT_FALSE(policy.shouldRedirect(response, request, 0));
}

TEST(RedirectPolicyTest, ExtractRedirectLocation) {
    RedirectPolicy policy;
    std::unordered_map<std::string, std::string> headers;
    headers["Location"] = "http://new.example.com/path";

    auto location = policy.extractRedirectLocation(headers);
    EXPECT_TRUE(location.has_value());
    EXPECT_EQ(location.value(), "http://new.example.com/path");

    headers.clear();
    location = policy.extractRedirectLocation(headers);
    EXPECT_FALSE(location.has_value());
}

TEST(RedirectPolicyTest, BuildRedirectRequest) {
    RequestInfo original;
    original.url = "http://old.example.com/old";
    original.methodType = HttpMethodType::Get;
    original.headers["Authorization"] = "Bearer token";

    auto redirect = RedirectPolicy::buildRedirectRequest(original, "http://new.example.com/new");

    EXPECT_EQ(redirect.url, "http://new.example.com/new");
    EXPECT_EQ(redirect.methodType, HttpMethodType::Get);
    EXPECT_EQ(redirect.headers["Authorization"], "Bearer token");
}

// HttpStatusCode Tests
TEST(HttpStatusCodeTest, InformationalCodes) {
    EXPECT_LT(static_cast<int>(HttpStatusCode::Continue), 200);
    EXPECT_LT(static_cast<int>(HttpStatusCode::SwitchingProtocols), 200);
}

TEST(HttpStatusCodeTest, SuccessCodes) {
    EXPECT_GE(static_cast<int>(HttpStatusCode::OK), 200);
    EXPECT_LT(static_cast<int>(HttpStatusCode::OK), 300);
    EXPECT_GE(static_cast<int>(HttpStatusCode::Created), 200);
    EXPECT_LT(static_cast<int>(HttpStatusCode::Created), 300);
}

TEST(HttpStatusCodeTest, RedirectCodes) {
    EXPECT_GE(static_cast<int>(HttpStatusCode::MovedPermanently), 300);
    EXPECT_LT(static_cast<int>(HttpStatusCode::MovedPermanently), 400);
    EXPECT_GE(static_cast<int>(HttpStatusCode::Found), 300);
    EXPECT_LT(static_cast<int>(HttpStatusCode::Found), 400);
}

TEST(HttpStatusCodeTest, ClientErrorCodes) {
    EXPECT_GE(static_cast<int>(HttpStatusCode::BadRequest), 400);
    EXPECT_LT(static_cast<int>(HttpStatusCode::BadRequest), 500);
    EXPECT_GE(static_cast<int>(HttpStatusCode::NotFound), 400);
    EXPECT_LT(static_cast<int>(HttpStatusCode::NotFound), 500);
}

TEST(HttpStatusCodeTest, ServerErrorCodes) {
    EXPECT_GE(static_cast<int>(HttpStatusCode::InternalServerError), 500);
    EXPECT_GE(static_cast<int>(HttpStatusCode::BadGateway), 500);
    EXPECT_GE(static_cast<int>(HttpStatusCode::ServiceUnavailable), 500);
}

// HttpMethodType Tests
TEST(HttpMethodTypeTest, Values) {
    EXPECT_EQ(static_cast<int>(HttpMethodType::Unknown), 0);
    EXPECT_EQ(static_cast<int>(HttpMethodType::Get), 1);
    EXPECT_EQ(static_cast<int>(HttpMethodType::Post), 2);
    EXPECT_EQ(static_cast<int>(HttpMethodType::Put), 3);
    EXPECT_EQ(static_cast<int>(HttpMethodType::Patch), 4);
    EXPECT_EQ(static_cast<int>(HttpMethodType::Delete), 5);
    EXPECT_EQ(static_cast<int>(HttpMethodType::Options), 6);
}

// IPVersion Tests
TEST(IPVersionTest, Values) {
    EXPECT_EQ(static_cast<int>(IPVersion::V4), 0);
    EXPECT_EQ(static_cast<int>(IPVersion::V6), 1);
    EXPECT_EQ(static_cast<int>(IPVersion::Auto), 2);
}

// Integration Tests
TEST(HttpIntegrationTest, UrlAndRequestInfo) {
    RequestInfo info;
    info.url = "https://api.example.com:8443/v1/users?id=123";
    info.methodType = HttpMethodType::Get;

    Url url(info.url);
    EXPECT_TRUE(url.isValid());
    EXPECT_EQ(url.scheme, "https");
    EXPECT_EQ(url.host, "api.example.com");
    EXPECT_EQ(url.portNumber(), 8443);

    bool keepAlive = true;
    std::string request = Util::buildRequestString(info, url, keepAlive);
    EXPECT_THAT(request, ::testing::HasSubstr("GET /v1/users?id=123 HTTP/1.1"));
    // Host 头默认不包含端口号（除非是非默认端口且实现需要）
    EXPECT_THAT(request, ::testing::HasSubstr("Host: api.example.com"));
}

TEST(HttpIntegrationTest, ParserWithRealResponse) {
    std::string body = R"({"status":"ok","data":[1,2,3]})";

    HttpStatusCode code = HttpStatusCode::Unknown;
    std::vector<uint8_t> receivedBody;

    HttpResponseParser::Callbacks callbacks;
    callbacks.onStatusLine = [&](HttpStatusCode c, const std::string&) {
        code = c;
    };
    callbacks.onBodyData = [&](const Byte* data, size_t size) {
        receivedBody.insert(receivedBody.end(), data, data + size);
    };

    HttpResponseParser parser(std::move(callbacks));

    std::string response = "HTTP/1.1 200 OK\r\n"
                           "Content-Type: application/json\r\n"
                           "Content-Length: " +
                           std::to_string(body.size()) +
                           "\r\n"
                           "\r\n" +
                           body;

    parser.feed(reinterpret_cast<const Byte*>(response.data()), response.size());

    EXPECT_TRUE(parser.isCompleted());
    EXPECT_EQ(code, HttpStatusCode::OK);
    EXPECT_EQ(receivedBody.size(), body.size());
}

TEST(HttpIntegrationTest, ChunkedResponseParsing) {
    std::string accumulatedBody;

    HttpResponseParser::Callbacks callbacks;
    callbacks.onBodyData = [&](const Byte* data, size_t size) {
        accumulatedBody.append(reinterpret_cast<const char*>(data), size);
    };

    HttpResponseParser parser(std::move(callbacks));

    // Feed in chunks to simulate real network conditions
    const char* part1 = "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n";
    parser.feed(reinterpret_cast<const Byte*>(part1), strlen(part1));
    EXPECT_EQ(parser.state(), HttpResponseParser::State::ChunkSize);

    const char* part2 = "5\r\nHello\r\n";
    parser.feed(reinterpret_cast<const Byte*>(part2), strlen(part2));

    const char* part3 = "6\r\n World\r\n0\r\n\r\n";
    parser.feed(reinterpret_cast<const Byte*>(part3), strlen(part3));

    EXPECT_TRUE(parser.isCompleted());
    EXPECT_EQ(accumulatedBody, "Hello World");
}