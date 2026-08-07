//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include "CancellationToken.h"
#include "HttpRequest.h"
#include "HttpResponseParser.h"
#include "HttpUtil.hpp"
#include "ThreadPool.hpp"
#include "Type.h"
#include "Url.h"

using namespace slark;
using namespace slark::http;

namespace {

size_t countRequestHeaders(const std::string& request, std::string_view name) {
    size_t count = 0;
    size_t lineStart = 0;
    while (lineStart < request.size()) {
        const auto lineEnd = request.find("\r\n", lineStart);
        if (lineEnd == std::string::npos) {
            break;
        }
        const auto line = std::string_view(request).substr(lineStart, lineEnd - lineStart);
        if (line.empty()) {
            break;
        }
        const auto colon = line.find(':');
        if (colon != std::string_view::npos && StringUtil::iequals(line.substr(0, colon), name)) {
            ++count;
        }
        lineStart = lineEnd + 2;
    }
    return count;
}

}  // namespace

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
    EXPECT_EQ(url.host, "::1");
    EXPECT_EQ(url.portNumber(), 8080);
}

TEST(UrlTest, ParseUrlWithUserInfo) {
    Url url("http://user:pass@example.com/resource");
    EXPECT_TRUE(url.isValid());
    EXPECT_EQ(url.userInfo, "user:pass");
    EXPECT_EQ(url.host, "example.com");
}

class HttpModuleParserTest : public ::testing::Test {
 protected:
    void SetUp() override {
        parser_ = std::make_unique<HttpResponseParser>(HttpResponseParser::Callbacks{});
    }

    std::unique_ptr<HttpResponseParser> parser_;
};

TEST_F(HttpModuleParserTest, ParseSimpleResponse) {
    const char* response = "HTTP/1.1 200 OK\r\n"
                           "Content-Length: 13\r\n"
                           "\r\n"
                           "Hello, World!";

    parser_->feed(reinterpret_cast<const Byte*>(response), strlen(response));
    EXPECT_TRUE(parser_->isCompleted());
}

TEST_F(HttpModuleParserTest, ParseChunkedResponse) {
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

TEST_F(HttpModuleParserTest, ParseNoContentLengthResponse) {
    const char* response = "HTTP/1.1 200 OK\r\n"
                           "Content-Type: text/plain\r\n"
                           "\r\n";

    parser_->feed(reinterpret_cast<const Byte*>(response), strlen(response));

    EXPECT_FALSE(parser_->isCompleted());
    EXPECT_TRUE(parser_->isBodyUnknownLength());

    parser_->onEof();
    EXPECT_TRUE(parser_->isCompleted());
}

TEST_F(HttpModuleParserTest, ParseHeaders) {
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
    EXPECT_FALSE(parser_->hasError());
}

TEST_F(HttpModuleParserTest, ShouldCloseConnection) {
    const char* http10 = "HTTP/1.0 200 OK\r\n"
                         "Content-Length: 0\r\n"
                         "\r\n";
    parser_->feed(reinterpret_cast<const Byte*>(http10), strlen(http10));
    EXPECT_TRUE(parser_->shouldCloseConnection());
    EXPECT_TRUE(parser_->isHttp10());

    parser_ = std::make_unique<HttpResponseParser>(HttpResponseParser::Callbacks{});
    const char* http11Close = "HTTP/1.1 200 OK\r\n"
                              "Connection: close\r\n"
                              "Content-Length: 0\r\n"
                              "\r\n";
    parser_->feed(reinterpret_cast<const Byte*>(http11Close), strlen(http11Close));
    EXPECT_TRUE(parser_->shouldCloseConnection());

    parser_ = std::make_unique<HttpResponseParser>(HttpResponseParser::Callbacks{});
    const char* http11Default = "HTTP/1.1 200 OK\r\n"
                                "Content-Length: 0\r\n"
                                "\r\n";
    parser_->feed(reinterpret_cast<const Byte*>(http11Default), strlen(http11Default));
    EXPECT_FALSE(parser_->shouldCloseConnection());
}

TEST_F(HttpModuleParserTest, StateTracking) {
    EXPECT_EQ(parser_->state(), HttpResponseParser::State::StatusLine);

    const char* partial = "HTTP/1.1 200 OK\r\n";
    parser_->feed(reinterpret_cast<const Byte*>(partial), strlen(partial));
    EXPECT_EQ(parser_->state(), HttpResponseParser::State::Headers);
}

TEST_F(HttpModuleParserTest, Reset) {
    const char* response = "HTTP/1.1 200 OK\r\n"
                           "Content-Length: 0\r\n"
                           "\r\n";

    parser_->feed(reinterpret_cast<const Byte*>(response), strlen(response));
    EXPECT_TRUE(parser_->isCompleted());

    parser_->reset();
    EXPECT_EQ(parser_->state(), HttpResponseParser::State::StatusLine);
    EXPECT_FALSE(parser_->isCompleted());
}

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

TEST(HttpUtilTest, BuildHeadRequest) {
    RequestInfo info;
    info.url = "http://example.com/path";
    info.methodType = HttpMethodType::Head;

    const Url url(info.url);
    const auto request = Util::buildRequestString(info, url, false);

    EXPECT_THAT(request, ::testing::HasSubstr("HEAD /path HTTP/1.1"));
}

TEST(HttpUtilTest, AllMethodNamesAreStable) {
    const std::array<std::pair<HttpMethodType, std::string_view>, 8> methods = {{
        {HttpMethodType::Unknown, "Unknown"}, {HttpMethodType::Get, "GET"},
        {HttpMethodType::Post, "POST"},       {HttpMethodType::Put, "PUT"},
        {HttpMethodType::Patch, "PATCH"},     {HttpMethodType::Delete, "DELETE"},
        {HttpMethodType::Options, "OPTIONS"}, {HttpMethodType::Head, "HEAD"},
    }};
    for (const auto& [method, name] : methods) {
        EXPECT_EQ(Util::getMethodName(method), name);
    }
}

TEST(HttpUtilTest, UnknownMethodIsRejectedBeforeSerialization) {
    RequestInfo info;
    info.url = "http://example.com/path";
    info.methodType = HttpMethodType::Unknown;
    const Url url(info.url);

    EXPECT_FALSE(Util::validateRequest(info, url));
    EXPECT_TRUE(Util::buildRequestString(info, url, false).empty());
}

TEST(HttpUtilTest, BuildRequestDoesNotDuplicateControlledHeaders) {
    RequestInfo info;
    info.url = "http://example.com/path";
    info.methodType = HttpMethodType::Post;
    info.headers = {{"host", "custom.example"}, {"CONTENT-LENGTH", "3"},
                    {"connection", "close"}};
    info.body = std::make_unique<Data>("abc");

    const Url url(info.url);
    const auto request = Util::buildRequestString(info, url, true);

    EXPECT_EQ(countRequestHeaders(request, "Host"), 1u);
    EXPECT_EQ(countRequestHeaders(request, "Content-Length"), 1u);
    EXPECT_EQ(countRequestHeaders(request, "Connection"), 1u);
    EXPECT_THAT(request, ::testing::HasSubstr("host: custom.example\r\n"));
    EXPECT_THAT(request, ::testing::HasSubstr("CONTENT-LENGTH: 3\r\n"));
    EXPECT_THAT(request, ::testing::HasSubstr("connection: close\r\n"));
}

TEST(HttpUtilTest, RejectsHeaderInjection) {
    for (const auto& header : {std::pair{"X-Test", "safe\r\nInjected: value"},
                               std::pair{"X\r\n-Test", "safe"}}) {
        RequestInfo info;
        info.url = "http://example.com/path";
        info.methodType = HttpMethodType::Get;
        info.headers = {{header.first, header.second}};

        const Url url(info.url);
        EXPECT_FALSE(Util::validateRequest(info, url));
        EXPECT_TRUE(Util::buildRequestString(info, url, false).empty());
    }
}

TEST(HttpUtilTest, RejectsCaseInsensitiveDuplicateHeaders) {
    RequestInfo info;
    info.url = "http://example.com/path";
    info.methodType = HttpMethodType::Get;
    info.headers = {{"X-Test", "one"}, {"x-test", "two"}};

    const Url url(info.url);
    EXPECT_FALSE(Util::validateRequest(info, url));
    EXPECT_TRUE(Util::buildRequestString(info, url, false).empty());
}

TEST(HttpUtilTest, RejectsInvalidRequestFramingHeaders) {
    const std::array<std::pair<std::string_view, std::string_view>, 5> invalidHeaders = {{
        {"Transfer-Encoding", "chunked"}, {"Connection", "upgrade"},
        {"Connection", "invalid"},      {"Content-Length", "-1"},
        {"Content-Length", "3abc"},
    }};
    for (const auto& [name, value] : invalidHeaders) {
        RequestInfo info;
        info.url = "http://example.com/path";
        info.methodType = HttpMethodType::Post;
        info.body = std::make_unique<Data>("abc");
        info.headers = {{std::string(name), std::string(value)}};

        const Url url(info.url);
        EXPECT_FALSE(Util::validateRequest(info, url)) << name << ": " << value;
        EXPECT_TRUE(Util::buildRequestString(info, url, false).empty()) << name << ": " << value;
    }
}

TEST(HttpUtilTest, RequestContentLengthMustMatchBody) {
    RequestInfo info;
    info.url = "http://example.com/path";
    info.methodType = HttpMethodType::Post;
    info.body = std::make_unique<Data>("abc");
    const Url url(info.url);

    for (const auto value : {"0", "2", "4", "9223372036854775808"}) {
        info.headers = {{"Content-Length", value}};
        EXPECT_FALSE(Util::validateRequest(info, url)) << "value=" << value;
        EXPECT_TRUE(Util::buildRequestString(info, url, false).empty()) << "value=" << value;
    }

    info.headers = {{"Content-Length", "3"}};
    const auto request = Util::buildRequestString(info, url, false);
    EXPECT_FALSE(request.empty());
    EXPECT_EQ(countRequestHeaders(request, "Content-Length"), 1u);
}

TEST(HttpUtilTest, EmptyBodyContentLengthMustBeZero) {
    RequestInfo info;
    info.url = "http://example.com/path";
    info.methodType = HttpMethodType::Post;
    const Url url(info.url);

    info.headers = {{"Content-Length", "3"}};
    EXPECT_FALSE(Util::validateRequest(info, url));
    info.headers = {{"Content-Length", "0"}};
    EXPECT_TRUE(Util::validateRequest(info, url));
}

TEST(HttpUtilTest, RequestHostAndConnectionContracts) {
    RequestInfo info;
    info.url = "http://example.com:8080/path";
    info.methodType = HttpMethodType::Get;
    const Url url(info.url);

    auto request = Util::buildRequestString(info, url, false);
    EXPECT_EQ(countRequestHeaders(request, "Host"), 1u);
    EXPECT_THAT(request, ::testing::HasSubstr("Host: example.com:8080\r\n"));
    EXPECT_THAT(request, ::testing::HasSubstr("Connection: close\r\n"));

    info.headers = {{"HOST", "custom.example"}, {"CONNECTION", "CLOSE"}};
    EXPECT_TRUE(Util::validateRequest(info, url));
    request = Util::buildRequestString(info, url, true);
    EXPECT_EQ(countRequestHeaders(request, "Host"), 1u);
    EXPECT_EQ(countRequestHeaders(request, "Connection"), 1u);
    EXPECT_THAT(request, ::testing::HasSubstr("HOST: custom.example\r\n"));
    EXPECT_THAT(request, ::testing::HasSubstr("CONNECTION: CLOSE\r\n"));
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
    EXPECT_FALSE(response.isSuccess());
    EXPECT_FALSE(response.hasValidResponse());
    EXPECT_FALSE(response.isTransportSuccess());
    EXPECT_FALSE(response.isHttpSuccess());
    EXPECT_FALSE(response.isOk());
}

TEST(TypeTest, HttpResponseSuccess) {
    HttpResponse response;
    response.statusCode = HttpStatusCode::OK;
    response.error = ErrorInfo{slark::Error::Success, 0};

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
    EXPECT_EQ(error.retCode, slark::Error::Success);
    EXPECT_EQ(error.originErrorCode, 0);
}

TEST(TypeTest, ErrorInfoConstruct) {
    ErrorInfo error(slark::Error::ConnectionTimeout, 5000);
    EXPECT_EQ(error.retCode, slark::Error::ConnectionTimeout);
    EXPECT_EQ(error.originErrorCode, 5000);
}

TEST(RedirectPolicyTest, ShouldRedirect) {
    RedirectPolicy policy;

    HttpResponse response;
    response.statusCode = HttpStatusCode::MovedPermanently;

    RequestInfo request;
    request.isAllowRedirect = true;

    EXPECT_TRUE(policy.shouldRedirect(response, request, 0));
    EXPECT_TRUE(policy.shouldRedirect(response, request, 5));
    EXPECT_FALSE(policy.shouldRedirect(response, request, 10));

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
    original.url = "http://example.com/old";
    original.methodType = HttpMethodType::Get;
    original.headers["Authorization"] = "Bearer token";

    auto redirect = RedirectPolicy::buildRedirectRequest(original, "http://example.com/new");

    ASSERT_TRUE(redirect.has_value());
    EXPECT_EQ(redirect->url, "http://example.com/new");
    EXPECT_EQ(redirect->methodType, HttpMethodType::Get);
    EXPECT_EQ(redirect->headers["Authorization"], "Bearer token");
}

TEST(RedirectPolicyTest, CrossDomainStripsSensitiveHeaders) {
    RequestInfo original;
    original.url = "http://old.example.com/old";
    original.methodType = HttpMethodType::Get;
    original.headers["Authorization"] = "Bearer token";

    auto redirect = RedirectPolicy::buildRedirectRequest(original, "http://new.example.com/new");

    ASSERT_TRUE(redirect.has_value());
    EXPECT_EQ(redirect->url, "http://new.example.com/new");
    EXPECT_TRUE(redirect->headers.find("Authorization") == redirect->headers.end() ||
                redirect->headers["Authorization"].empty());
}

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

TEST(HttpMethodTypeTest, Values) {
    EXPECT_EQ(static_cast<int>(HttpMethodType::Unknown), 0);
    EXPECT_EQ(static_cast<int>(HttpMethodType::Get), 1);
    EXPECT_EQ(static_cast<int>(HttpMethodType::Post), 2);
    EXPECT_EQ(static_cast<int>(HttpMethodType::Put), 3);
    EXPECT_EQ(static_cast<int>(HttpMethodType::Patch), 4);
    EXPECT_EQ(static_cast<int>(HttpMethodType::Delete), 5);
    EXPECT_EQ(static_cast<int>(HttpMethodType::Options), 6);
}

TEST(IPVersionTest, Values) {
    EXPECT_EQ(static_cast<int>(IPVersion::V4), 0);
    EXPECT_EQ(static_cast<int>(IPVersion::V6), 1);
    EXPECT_EQ(static_cast<int>(IPVersion::Auto), 2);
}

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
    EXPECT_THAT(request, ::testing::HasSubstr("Host: api.example.com"));
}

TEST(HttpResponseTest, SuccessSemanticsDistinguishTransportAndHttpStatus) {
    HttpResponse empty;
    EXPECT_FALSE(empty.hasValidResponse());
    EXPECT_FALSE(empty.isTransportSuccess());
    EXPECT_FALSE(empty.isSuccess());

    HttpResponse ok;
    ok.statusCode = HttpStatusCode::OK;
    ok.error = ErrorInfo{Error::Success};
    EXPECT_TRUE(ok.hasValidResponse());
    EXPECT_TRUE(ok.isTransportSuccess());
    EXPECT_TRUE(ok.isHttpSuccess());
    EXPECT_TRUE(ok.isSuccess());

    HttpResponse redirect;
    redirect.statusCode = HttpStatusCode::Found;
    redirect.error = ErrorInfo{Error::Success};
    EXPECT_TRUE(redirect.isTransportSuccess());
    EXPECT_FALSE(redirect.isHttpSuccess());
    EXPECT_TRUE(redirect.isSuccess());

    HttpResponse error;
    error.statusCode = HttpStatusCode::NotFound;
    error.error = ErrorInfo{Error::HttpError};
    EXPECT_TRUE(error.hasValidResponse());
    EXPECT_FALSE(error.isTransportSuccess());
    EXPECT_FALSE(error.isHttpSuccess());
    EXPECT_FALSE(error.isSuccess());
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