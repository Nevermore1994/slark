//
// Created by Nevermore on 2024/12/6.
// slark RequestTest - Real HTTP request integration tests
// Copyright (c) 2024 Nevermore All rights reserved.
//
#include "CancellationToken.h"
#include "CoroutineScheduler.hpp"
#include "Data.h"
#include "HttpRequest.h"
#include "Type.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <gtest/gtest.h>
#include <mutex>

using namespace slark;
using namespace slark::http;

namespace {

constexpr auto kTestTimeout = std::chrono::seconds(30);
constexpr const char* kBaseUrl = "http://httpbin.org";

struct SyncWaiter {
    std::mutex mutex;
    std::condition_variable cond;
    std::atomic<bool> finished{false};

    void notify() {
        {
            std::lock_guard lock(mutex);
            finished = true;
        }
        cond.notify_all();
    }

    bool wait() {
        std::unique_lock lock(mutex);
        return cond.wait_for(lock, kTestTimeout, [this] {
            return finished.load();
        });
    }
};

RequestInfo makeRequest(std::string_view path, HttpMethodType method, DataRefPtr body = nullptr) {
    RequestInfo info;
    info.url = std::string(kBaseUrl) + std::string(path);
    info.methodType = method;
    info.headers = {{"Content-Type", "application/json"},
                    {"User-Agent", "slark-test/1.0"},
                    {"Accept", "*/*"}};
    info.body = std::move(body);
    info.connectTimeout = std::chrono::seconds(10);
    info.readTimeout = std::chrono::seconds(20);
    return info;
}

// 通过回调模式执行请求并同步等待结果
HttpResponse executeSync(const RequestInfo& info) {
    auto waiter = std::make_shared<SyncWaiter>();
    auto result = std::make_shared<HttpResponse>();

    HttpResponseHandler handler;
    handler.onHeader = [result](const RequestInfo&, ResponseHeader&& header) {
        result->statusCode = header.httpStatusCode;
        result->headers = std::move(header.headers);
    };
    handler.onData = [result](const RequestInfo&, DataPtr data) {
        if (data) {
            if (!result->body) {
                result->body = std::move(data);
            } else {
                result->body->append(ConstByteSpan(data->data(), data->size()));
            }
        }
    };
    handler.onCompleted = [result, waiter](const RequestInfo&) {
        result->error = ErrorInfo{ResultCode::Success, 0};
        waiter->notify();
    };
    handler.onError = [result, waiter](const RequestInfo&, ErrorInfo error) {
        result->error = error;
        waiter->notify();
    };

    // shared_ptr 延长生命周期，避免协程未完成时对象析构
    auto request = std::make_shared<HttpRequest>();
    request->asyncExecute(info, std::move(handler));

    EXPECT_TRUE(waiter->wait()) << "Request timed out: " << info.url;
    return std::move(*result);
}

} // namespace

class HttpRequestTest : public ::testing::Test {
protected:
    void SetUp() override {}

    void TearDown() override {}
};

TEST_F(HttpRequestTest, Get) {
    auto info = makeRequest("/get", HttpMethodType::Get);
    auto response = executeSync(info);

    EXPECT_EQ(response.error.retCode, ResultCode::Success);
    EXPECT_EQ(response.statusCode, HttpStatusCode::OK);
    EXPECT_NE(response.body, nullptr);
}

TEST_F(HttpRequestTest, Post) {
    auto body = std::make_unique<Data>(R"({"name": "JohnDoe", "age": 30})");
    auto info = makeRequest("/post", HttpMethodType::Post, std::move(body));
    auto response = executeSync(info);

    EXPECT_EQ(response.error.retCode, ResultCode::Success);
    EXPECT_EQ(response.statusCode, HttpStatusCode::OK);
    EXPECT_NE(response.body, nullptr);
}

TEST_F(HttpRequestTest, Put) {
    auto body = std::make_unique<Data>(R"({"name": "JohnDoe", "age": 30})");
    auto info = makeRequest("/put", HttpMethodType::Put, std::move(body));
    auto response = executeSync(info);

    EXPECT_EQ(response.error.retCode, ResultCode::Success);
    EXPECT_EQ(response.statusCode, HttpStatusCode::OK);
}

TEST_F(HttpRequestTest, Delete) {
    auto info = makeRequest("/delete", HttpMethodType::Delete);
    auto response = executeSync(info);

    EXPECT_EQ(response.error.retCode, ResultCode::Success);
    EXPECT_EQ(response.statusCode, HttpStatusCode::OK);
}

TEST_F(HttpRequestTest, Patch) {
    auto body = std::make_unique<Data>(R"({"name": "JohnDoe", "age": 30})");
    auto info = makeRequest("/patch", HttpMethodType::Patch, std::move(body));
    auto response = executeSync(info);

    EXPECT_EQ(response.error.retCode, ResultCode::Success);
    EXPECT_EQ(response.statusCode, HttpStatusCode::OK);
}

TEST_F(HttpRequestTest, Options) {
    auto info = makeRequest("/anything", HttpMethodType::Options);
    info.headers["Access-Control-Request-Method"] = "POST";
    info.headers["Access-Control-Request-Headers"] = "X-PINGOTHER, Content-Type";
    auto response = executeSync(info);

    EXPECT_EQ(response.error.retCode, ResultCode::Success);
    EXPECT_EQ(response.statusCode, HttpStatusCode::OK);
}

TEST_F(HttpRequestTest, Redirect) {
    auto info = makeRequest("/redirect-to?url=http%3A%2F%2Fhttpbin.org%2Fget&status_code=302",
                            HttpMethodType::Get);
    info.isAllowRedirect = true;
    auto response = executeSync(info);

    EXPECT_EQ(response.error.retCode, ResultCode::Success);
    EXPECT_EQ(response.statusCode, HttpStatusCode::OK);
}

TEST_F(HttpRequestTest, Status404) {
    auto info = makeRequest("/status/404", HttpMethodType::Get);
    auto response = executeSync(info);

    EXPECT_EQ(response.error.retCode, ResultCode::Success);
    EXPECT_EQ(response.statusCode, HttpStatusCode::NotFound);
}

TEST_F(HttpRequestTest, CancelToken) {
    // 验证 CancellationToken 的基本功能
    CancellationToken token;
    EXPECT_FALSE(token.isCancelled());

    token.cancel();
    EXPECT_TRUE(token.isCancelled());

    // 验证 HttpRequest::cancel() 接口存在且可调用
    HttpRequest request;
    request.cancel(); // 无 bridge 时不崩溃
}

TEST_F(HttpRequestTest, ConnectTimeout) {
    RequestInfo info;
    // 使用不可达地址触发连接超时
    info.url = "http://192.0.2.1/timeout";
    info.methodType = HttpMethodType::Get;
    info.connectTimeout = std::chrono::seconds(3);
    info.readTimeout = std::chrono::seconds(3);

    auto response = executeSync(info);
    EXPECT_NE(response.error.retCode, ResultCode::Success);
}

// HTTPS 测试用例
class HttpsRequestTest : public ::testing::Test {
protected:
    void SetUp() override {}

    void TearDown() override {}
};

TEST_F(HttpsRequestTest, Get) {
    RequestInfo info;
    info.url = "https://httpbin.org/get";
    info.methodType = HttpMethodType::Get;
    info.headers = {{"User-Agent", "slark-https-test/1.0"}, {"Accept", "*/*"}};
    info.connectTimeout = std::chrono::seconds(10);
    info.readTimeout = std::chrono::seconds(20);

    auto response = executeSync(info);

    EXPECT_EQ(response.error.retCode, ResultCode::Success);
    EXPECT_EQ(response.statusCode, HttpStatusCode::OK);
    EXPECT_NE(response.body, nullptr);
}

TEST_F(HttpsRequestTest, Post) {
    RequestInfo info;
    info.url = "https://httpbin.org/post";
    info.methodType = HttpMethodType::Post;
    info.headers = {{"Content-Type", "application/json"}, {"User-Agent", "slark-https-test/1.0"}};
    info.body = std::make_unique<Data>(R"({"test": "https", "value": 42})");
    info.connectTimeout = std::chrono::seconds(10);
    info.readTimeout = std::chrono::seconds(20);

    auto response = executeSync(info);

    EXPECT_EQ(response.error.retCode, ResultCode::Success);
    EXPECT_EQ(response.statusCode, HttpStatusCode::OK);
}

TEST_F(HttpsRequestTest, LargeResponse) {
    RequestInfo info;
    info.url = "https://httpbin.org/bytes/65536"; // 64KB
    info.methodType = HttpMethodType::Get;
    info.headers = {{"User-Agent", "slark-https-test/1.0"}};
    info.connectTimeout = std::chrono::seconds(10);
    info.readTimeout = std::chrono::seconds(30);

    auto response = executeSync(info);

    EXPECT_EQ(response.error.retCode, ResultCode::Success);
    EXPECT_EQ(response.statusCode, HttpStatusCode::OK);
    ASSERT_NE(response.body, nullptr);
    EXPECT_GE(response.body->size(), 65536u);
}

TEST_F(HttpsRequestTest, Put) {
    RequestInfo info;
    info.url = "https://httpbin.org/put";
    info.methodType = HttpMethodType::Put;
    info.headers = {{"Content-Type", "application/json"}, {"User-Agent", "slark-https-test/1.0"}};
    info.body = std::make_unique<Data>(R"({"name": "https-put-test", "version": 2})");
    info.connectTimeout = std::chrono::seconds(10);
    info.readTimeout = std::chrono::seconds(20);

    auto response = executeSync(info);

    EXPECT_EQ(response.error.retCode, ResultCode::Success);
    EXPECT_EQ(response.statusCode, HttpStatusCode::OK);
}

TEST_F(HttpsRequestTest, Delete) {
    RequestInfo info;
    info.url = "https://httpbin.org/delete";
    info.methodType = HttpMethodType::Delete;
    info.headers = {{"User-Agent", "slark-https-test/1.0"}};
    info.connectTimeout = std::chrono::seconds(10);
    info.readTimeout = std::chrono::seconds(20);

    auto response = executeSync(info);

    EXPECT_EQ(response.error.retCode, ResultCode::Success);
    EXPECT_EQ(response.statusCode, HttpStatusCode::OK);
}

TEST_F(HttpsRequestTest, Patch) {
    RequestInfo info;
    info.url = "https://httpbin.org/patch";
    info.methodType = HttpMethodType::Patch;
    info.headers = {{"Content-Type", "application/json"}, {"User-Agent", "slark-https-test/1.0"}};
    info.body = std::make_unique<Data>(R"({"field": "patched", "value": 123})");
    info.connectTimeout = std::chrono::seconds(10);
    info.readTimeout = std::chrono::seconds(20);

    auto response = executeSync(info);

    EXPECT_EQ(response.error.retCode, ResultCode::Success);
    EXPECT_EQ(response.statusCode, HttpStatusCode::OK);
}

TEST_F(HttpsRequestTest, Options) {
    RequestInfo info;
    info.url = "https://httpbin.org/anything";
    info.methodType = HttpMethodType::Options;
    info.headers = {{"User-Agent", "slark-https-test/1.0"},
                    {"Access-Control-Request-Method", "POST"}};
    info.connectTimeout = std::chrono::seconds(10);
    info.readTimeout = std::chrono::seconds(20);

    auto response = executeSync(info);

    EXPECT_EQ(response.error.retCode, ResultCode::Success);
    EXPECT_EQ(response.statusCode, HttpStatusCode::OK);
}

TEST_F(HttpsRequestTest, Redirect) {
    RequestInfo info;
    info.url =
        "https://httpbin.org/redirect-to?url=https%3A%2F%2Fhttpbin.org%2Fget&status_code=302";
    info.methodType = HttpMethodType::Get;
    info.headers = {{"User-Agent", "slark-https-test/1.0"}};
    info.isAllowRedirect = true;
    info.connectTimeout = std::chrono::seconds(10);
    info.readTimeout = std::chrono::seconds(30);

    auto response = executeSync(info);

    EXPECT_EQ(response.error.retCode, ResultCode::Success);
    EXPECT_EQ(response.statusCode, HttpStatusCode::OK);
}

TEST_F(HttpsRequestTest, Status404) {
    RequestInfo info;
    info.url = "https://httpbin.org/status/404";
    info.methodType = HttpMethodType::Get;
    info.headers = {{"User-Agent", "slark-https-test/1.0"}};
    info.connectTimeout = std::chrono::seconds(10);
    info.readTimeout = std::chrono::seconds(20);

    auto response = executeSync(info);

    EXPECT_EQ(response.error.retCode, ResultCode::Success);
    EXPECT_EQ(response.statusCode, HttpStatusCode::NotFound);
}

TEST_F(HttpsRequestTest, HeadersEcho) {
    RequestInfo info;
    info.url = "https://httpbin.org/headers";
    info.methodType = HttpMethodType::Get;
    info.headers = {{"User-Agent", "slark-https-test/1.0"},
                    {"X-Custom-Header", "custom-value-123"},
                    {"X-Test-Id", "test-456"}};
    info.connectTimeout = std::chrono::seconds(10);
    info.readTimeout = std::chrono::seconds(20);

    auto response = executeSync(info);

    EXPECT_EQ(response.error.retCode, ResultCode::Success);
    EXPECT_EQ(response.statusCode, HttpStatusCode::OK);
    ASSERT_NE(response.body, nullptr);
    // 验证响应中包含我们发送的请求头
    auto bodyStr =
        std::string(reinterpret_cast<const char*>(response.body->data()), response.body->size());
    EXPECT_TRUE(bodyStr.find("X-Custom-Header") != std::string::npos);
    EXPECT_TRUE(bodyStr.find("custom-value-123") != std::string::npos);
}

TEST_F(HttpsRequestTest, QueryParams) {
    RequestInfo info;
    info.url = "https://httpbin.org/get?foo=bar&name=test&value=42";
    info.methodType = HttpMethodType::Get;
    info.headers = {{"User-Agent", "slark-https-test/1.0"}};
    info.connectTimeout = std::chrono::seconds(10);
    info.readTimeout = std::chrono::seconds(20);

    auto response = executeSync(info);

    EXPECT_EQ(response.error.retCode, ResultCode::Success);
    EXPECT_EQ(response.statusCode, HttpStatusCode::OK);
    ASSERT_NE(response.body, nullptr);
    auto bodyStr =
        std::string(reinterpret_cast<const char*>(response.body->data()), response.body->size());
    EXPECT_TRUE(bodyStr.find("\"foo\": \"bar\"") != std::string::npos ||
                bodyStr.find("\"foo\":\"bar\"") != std::string::npos);
    EXPECT_TRUE(bodyStr.find("\"name\": \"test\"") != std::string::npos ||
                bodyStr.find("\"name\":\"test\"") != std::string::npos);
}

// HTTP 高级场景测试
class HttpAdvancedTest : public ::testing::Test {
protected:
    void SetUp() override {}

    void TearDown() override {}
};

TEST_F(HttpAdvancedTest, Status500) {
    auto info = makeRequest("/status/500", HttpMethodType::Get);
    auto response = executeSync(info);

    EXPECT_EQ(response.error.retCode, ResultCode::Success);
    EXPECT_EQ(response.statusCode, HttpStatusCode::InternalServerError);
}

TEST_F(HttpAdvancedTest, Status403) {
    auto info = makeRequest("/status/403", HttpMethodType::Get);
    auto response = executeSync(info);

    EXPECT_EQ(response.error.retCode, ResultCode::Success);
    EXPECT_EQ(response.statusCode, HttpStatusCode::Forbidden);
}

TEST_F(HttpAdvancedTest, DelayedResponse) {
    auto info = makeRequest("/delay/2", HttpMethodType::Get);
    info.readTimeout = std::chrono::seconds(10);
    auto response = executeSync(info);

    EXPECT_EQ(response.error.retCode, ResultCode::Success);
    EXPECT_EQ(response.statusCode, HttpStatusCode::OK);
}

TEST_F(HttpAdvancedTest, GzipResponse) {
    auto info = makeRequest("/gzip", HttpMethodType::Get);
    info.headers["Accept-Encoding"] = "gzip";
    auto response = executeSync(info);

    EXPECT_EQ(response.error.retCode, ResultCode::Success);
    EXPECT_EQ(response.statusCode, HttpStatusCode::OK);
}

TEST_F(HttpAdvancedTest, BasicAuth_Success) {
    auto info = makeRequest("/basic-auth/testuser/testpass", HttpMethodType::Get);
    info.headers["Authorization"] = "Basic dGVzdHVzZXI6dGVzdHBhc3M="; // base64(testuser:testpass)
    auto response = executeSync(info);

    EXPECT_EQ(response.error.retCode, ResultCode::Success);
    EXPECT_EQ(response.statusCode, HttpStatusCode::OK);
}

TEST_F(HttpAdvancedTest, BasicAuth_Failure) {
    auto info = makeRequest("/basic-auth/testuser/testpass", HttpMethodType::Get);
    // 不发送认证头，期望 401
    auto response = executeSync(info);

    EXPECT_EQ(response.error.retCode, ResultCode::Success);
    EXPECT_EQ(response.statusCode, HttpStatusCode::Unauthorized);
}

TEST_F(HttpAdvancedTest, MultipleRedirects) {
    auto info = makeRequest("/redirect/2", HttpMethodType::Get);
    info.isAllowRedirect = true;
    info.connectTimeout = std::chrono::seconds(15);
    info.readTimeout = std::chrono::seconds(45);
    auto response = executeSync(info);

    EXPECT_EQ(response.error.retCode, ResultCode::Success);
    EXPECT_EQ(response.statusCode, HttpStatusCode::OK);
}

TEST_F(HttpAdvancedTest, ResponseHeaders) {
    auto info = makeRequest("/response-headers?X-Custom=hello&X-Test=world", HttpMethodType::Get);
    auto response = executeSync(info);

    EXPECT_EQ(response.error.retCode, ResultCode::Success);
    EXPECT_EQ(response.statusCode, HttpStatusCode::OK);
    // 验证响应头存在
    EXPECT_FALSE(response.headers.empty());
}

TEST_F(HttpAdvancedTest, PostFormUrlEncoded) {
    RequestInfo info;
    info.url = std::string(kBaseUrl) + "/post";
    info.methodType = HttpMethodType::Post;
    info.headers = {{"Content-Type", "application/x-www-form-urlencoded"},
                    {"User-Agent", "slark-test/1.0"}};
    info.body = std::make_unique<Data>("field1=value1&field2=value2");
    info.connectTimeout = std::chrono::seconds(10);
    info.readTimeout = std::chrono::seconds(20);

    auto response = executeSync(info);

    EXPECT_EQ(response.error.retCode, ResultCode::Success);
    EXPECT_EQ(response.statusCode, HttpStatusCode::OK);
    ASSERT_NE(response.body, nullptr);
    auto bodyStr =
        std::string(reinterpret_cast<const char*>(response.body->data()), response.body->size());
    EXPECT_TRUE(bodyStr.find("field1") != std::string::npos);
    EXPECT_TRUE(bodyStr.find("value1") != std::string::npos);
}

// 协程式HTTP测试辅助函数
HttpResponse executeCoroutine(const RequestInfo& info) {
    auto waiter = std::make_shared<SyncWaiter>();
    auto result = std::make_shared<HttpResponse>();

    auto testCoroutine = [&]() -> Coroutine<void> {
        try {
            auto request = std::make_shared<HttpRequest>();
            auto response = co_await request->asyncExecute(info);
            *result = std::move(response);
        } catch (const std::exception& e) {
            result->error = ErrorInfo{ResultCode::Failed, 0};
        }
        waiter->notify();
    };

    auto coro = testCoroutine();
    CoroutineScheduler::instance().schedule(coro);

    EXPECT_TRUE(waiter->wait()) << "Coroutine request timed out: " << info.url;
    return std::move(*result);
}

// 协程式HTTP测试
class HttpCoroutineTest : public ::testing::Test {
protected:
    void SetUp() override {}

    void TearDown() override {}
};

TEST_F(HttpCoroutineTest, Get) {
    auto info = makeRequest("/get", HttpMethodType::Get);
    auto response = executeCoroutine(info);

    EXPECT_EQ(response.error.retCode, ResultCode::Success);
    EXPECT_EQ(response.statusCode, HttpStatusCode::OK);
    EXPECT_NE(response.body, nullptr);
}

TEST_F(HttpCoroutineTest, Post) {
    auto body = std::make_unique<Data>(R"({"name": "JohnDoe", "age": 30})");
    auto info = makeRequest("/post", HttpMethodType::Post, std::move(body));
    auto response = executeCoroutine(info);

    EXPECT_EQ(response.error.retCode, ResultCode::Success);
    EXPECT_EQ(response.statusCode, HttpStatusCode::OK);
    EXPECT_NE(response.body, nullptr);
}

TEST_F(HttpCoroutineTest, Put) {
    auto body = std::make_unique<Data>(R"({"name": "JohnDoe", "age": 30})");
    auto info = makeRequest("/put", HttpMethodType::Put, std::move(body));
    auto response = executeCoroutine(info);

    EXPECT_EQ(response.error.retCode, ResultCode::Success);
    EXPECT_EQ(response.statusCode, HttpStatusCode::OK);
}

TEST_F(HttpCoroutineTest, Delete) {
    auto info = makeRequest("/delete", HttpMethodType::Delete);
    auto response = executeCoroutine(info);

    EXPECT_EQ(response.error.retCode, ResultCode::Success);
    EXPECT_EQ(response.statusCode, HttpStatusCode::OK);
}

TEST_F(HttpCoroutineTest, Patch) {
    auto body = std::make_unique<Data>(R"({"name": "JohnDoe", "age": 30})");
    auto info = makeRequest("/patch", HttpMethodType::Patch, std::move(body));
    auto response = executeCoroutine(info);

    EXPECT_EQ(response.error.retCode, ResultCode::Success);
    EXPECT_EQ(response.statusCode, HttpStatusCode::OK);
}

TEST_F(HttpCoroutineTest, Redirect) {
    auto info = makeRequest("/redirect-to?url=http%3A%2F%2Fhttpbin.org%2Fget&status_code=302",
                            HttpMethodType::Get);
    info.isAllowRedirect = true;
    auto response = executeCoroutine(info);

    EXPECT_EQ(response.error.retCode, ResultCode::Success);
    EXPECT_EQ(response.statusCode, HttpStatusCode::OK);
}

TEST_F(HttpCoroutineTest, MultipleRedirects) {
    auto info = makeRequest("/redirect/2", HttpMethodType::Get);
    info.isAllowRedirect = true;
    info.connectTimeout = std::chrono::seconds(15);
    info.readTimeout = std::chrono::seconds(45);
    auto response = executeCoroutine(info);

    EXPECT_EQ(response.error.retCode, ResultCode::Success);
    EXPECT_EQ(response.statusCode, HttpStatusCode::OK);
}

TEST_F(HttpCoroutineTest, Status404) {
    auto info = makeRequest("/status/404", HttpMethodType::Get);
    auto response = executeCoroutine(info);

    EXPECT_EQ(response.error.retCode, ResultCode::Success);
    EXPECT_EQ(response.statusCode, HttpStatusCode::NotFound);
}

TEST_F(HttpCoroutineTest, DelayedResponse) {
    auto info = makeRequest("/delay/2", HttpMethodType::Get);
    info.readTimeout = std::chrono::seconds(10);
    auto response = executeCoroutine(info);

    EXPECT_EQ(response.error.retCode, ResultCode::Success);
    EXPECT_EQ(response.statusCode, HttpStatusCode::OK);
}

TEST_F(HttpCoroutineTest, GzipResponse) {
    auto info = makeRequest("/gzip", HttpMethodType::Get);
    info.headers["Accept-Encoding"] = "gzip";
    auto response = executeCoroutine(info);

    EXPECT_EQ(response.error.retCode, ResultCode::Success);
    EXPECT_EQ(response.statusCode, HttpStatusCode::OK);
}

TEST_F(HttpCoroutineTest, PostFormUrlEncoded) {
    RequestInfo info;
    info.url = std::string(kBaseUrl) + "/post";
    info.methodType = HttpMethodType::Post;
    info.headers = {{"Content-Type", "application/x-www-form-urlencoded"},
                    {"User-Agent", "slark-test/1.0"}};
    info.body = std::make_unique<Data>("field1=value1&field2=value2");
    info.connectTimeout = std::chrono::seconds(10);
    info.readTimeout = std::chrono::seconds(20);

    auto response = executeCoroutine(info);

    EXPECT_EQ(response.error.retCode, ResultCode::Success);
    EXPECT_EQ(response.statusCode, HttpStatusCode::OK);
    ASSERT_NE(response.body, nullptr);
    auto bodyStr =
        std::string(reinterpret_cast<const char*>(response.body->data()), response.body->size());
    EXPECT_TRUE(bodyStr.find("field1") != std::string::npos);
    EXPECT_TRUE(bodyStr.find("value1") != std::string::npos);
}

// 协程式HTTPS测试
class HttpsCoroutineTest : public ::testing::Test {
protected:
    void SetUp() override {}

    void TearDown() override {}
};

TEST_F(HttpsCoroutineTest, Get) {
    RequestInfo info;
    info.url = "https://httpbin.org/get";
    info.methodType = HttpMethodType::Get;
    info.headers = {{"User-Agent", "slark-https-coroutine-test/1.0"}, {"Accept", "*/*"}};
    info.connectTimeout = std::chrono::seconds(10);
    info.readTimeout = std::chrono::seconds(20);

    auto response = executeCoroutine(info);

    EXPECT_EQ(response.error.retCode, ResultCode::Success);
    EXPECT_EQ(response.statusCode, HttpStatusCode::OK);
    EXPECT_NE(response.body, nullptr);
}

TEST_F(HttpsCoroutineTest, Post) {
    RequestInfo info;
    info.url = "https://httpbin.org/post";
    info.methodType = HttpMethodType::Post;
    info.headers = {{"Content-Type", "application/json"},
                    {"User-Agent", "slark-https-coroutine-test/1.0"}};
    info.body = std::make_unique<Data>(R"({"test": "https", "value": 42})");
    info.connectTimeout = std::chrono::seconds(10);
    info.readTimeout = std::chrono::seconds(20);

    auto response = executeCoroutine(info);

    EXPECT_EQ(response.error.retCode, ResultCode::Success);
    EXPECT_EQ(response.statusCode, HttpStatusCode::OK);
}

TEST_F(HttpsCoroutineTest, Put) {
    RequestInfo info;
    info.url = "https://httpbin.org/put";
    info.methodType = HttpMethodType::Put;
    info.headers = {{"Content-Type", "application/json"},
                    {"User-Agent", "slark-https-coroutine-test/1.0"}};
    info.body = std::make_unique<Data>(R"({"name": "https-put-test", "version": 2})");
    info.connectTimeout = std::chrono::seconds(10);
    info.readTimeout = std::chrono::seconds(20);

    auto response = executeCoroutine(info);

    EXPECT_EQ(response.error.retCode, ResultCode::Success);
    EXPECT_EQ(response.statusCode, HttpStatusCode::OK);
}

TEST_F(HttpsCoroutineTest, Delete) {
    RequestInfo info;
    info.url = "https://httpbin.org/delete";
    info.methodType = HttpMethodType::Delete;
    info.headers = {{"User-Agent", "slark-https-coroutine-test/1.0"}};
    info.connectTimeout = std::chrono::seconds(10);
    info.readTimeout = std::chrono::seconds(20);

    auto response = executeCoroutine(info);

    EXPECT_EQ(response.error.retCode, ResultCode::Success);
    EXPECT_EQ(response.statusCode, HttpStatusCode::OK);
}

TEST_F(HttpsCoroutineTest, Patch) {
    RequestInfo info;
    info.url = "https://httpbin.org/patch";
    info.methodType = HttpMethodType::Patch;
    info.headers = {{"Content-Type", "application/json"},
                    {"User-Agent", "slark-https-coroutine-test/1.0"}};
    info.body = std::make_unique<Data>(R"({"field": "patched", "value": 123})");
    info.connectTimeout = std::chrono::seconds(10);
    info.readTimeout = std::chrono::seconds(20);

    auto response = executeCoroutine(info);

    EXPECT_EQ(response.error.retCode, ResultCode::Success);
    EXPECT_EQ(response.statusCode, HttpStatusCode::OK);
}

TEST_F(HttpsCoroutineTest, Redirect) {
    RequestInfo info;
    info.url =
        "https://httpbin.org/redirect-to?url=https%3A%2F%2Fhttpbin.org%2Fget&status_code=302";
    info.methodType = HttpMethodType::Get;
    info.headers = {{"User-Agent", "slark-https-coroutine-test/1.0"}};
    info.isAllowRedirect = true;
    info.connectTimeout = std::chrono::seconds(10);
    info.readTimeout = std::chrono::seconds(30);

    auto response = executeCoroutine(info);

    EXPECT_EQ(response.error.retCode, ResultCode::Success);
    EXPECT_EQ(response.statusCode, HttpStatusCode::OK);
}

TEST_F(HttpsCoroutineTest, MultipleRedirects) {
    RequestInfo info;
    info.url = "https://httpbin.org/redirect/2";
    info.methodType = HttpMethodType::Get;
    info.headers = {{"User-Agent", "slark-https-coroutine-test/1.0"}};
    info.isAllowRedirect = true;
    info.connectTimeout = std::chrono::seconds(15);
    info.readTimeout = std::chrono::seconds(45);

    auto response = executeCoroutine(info);

    EXPECT_EQ(response.error.retCode, ResultCode::Success);
    EXPECT_EQ(response.statusCode, HttpStatusCode::OK);
}

TEST_F(HttpsCoroutineTest, LargeResponse) {
    RequestInfo info;
    info.url = "https://httpbin.org/bytes/65536"; // 64KB
    info.methodType = HttpMethodType::Get;
    info.headers = {{"User-Agent", "slark-https-coroutine-test/1.0"}};
    info.connectTimeout = std::chrono::seconds(10);
    info.readTimeout = std::chrono::seconds(30);

    auto response = executeCoroutine(info);

    EXPECT_EQ(response.error.retCode, ResultCode::Success);
    EXPECT_EQ(response.statusCode, HttpStatusCode::OK);
    ASSERT_NE(response.body, nullptr);
    EXPECT_GE(response.body->size(), 65536u);
}

TEST_F(HttpsCoroutineTest, Status404) {
    RequestInfo info;
    info.url = "https://httpbin.org/status/404";
    info.methodType = HttpMethodType::Get;
    info.headers = {{"User-Agent", "slark-https-coroutine-test/1.0"}};
    info.connectTimeout = std::chrono::seconds(10);
    info.readTimeout = std::chrono::seconds(20);

    auto response = executeCoroutine(info);

    EXPECT_EQ(response.error.retCode, ResultCode::Success);
    EXPECT_EQ(response.statusCode, HttpStatusCode::NotFound);
}

TEST_F(HttpsCoroutineTest, DelayedResponse) {
    RequestInfo info;
    info.url = "https://httpbin.org/delay/2";
    info.methodType = HttpMethodType::Get;
    info.headers = {{"User-Agent", "slark-https-coroutine-test/1.0"}};
    info.readTimeout = std::chrono::seconds(10);

    auto response = executeCoroutine(info);

    EXPECT_EQ(response.error.retCode, ResultCode::Success);
    EXPECT_EQ(response.statusCode, HttpStatusCode::OK);
}

TEST_F(HttpsCoroutineTest, HeadersEcho) {
    RequestInfo info;
    info.url = "https://httpbin.org/headers";
    info.methodType = HttpMethodType::Get;
    info.headers = {{"User-Agent", "slark-https-coroutine-test/1.0"},
                    {"X-Custom-Header", "custom-value-123"},
                    {"X-Test-Id", "test-456"}};
    info.connectTimeout = std::chrono::seconds(10);
    info.readTimeout = std::chrono::seconds(20);

    auto response = executeCoroutine(info);

    EXPECT_EQ(response.error.retCode, ResultCode::Success);
    EXPECT_EQ(response.statusCode, HttpStatusCode::OK);
    ASSERT_NE(response.body, nullptr);
    auto bodyStr =
        std::string(reinterpret_cast<const char*>(response.body->data()), response.body->size());
    EXPECT_TRUE(bodyStr.find("X-Custom-Header") != std::string::npos);
    EXPECT_TRUE(bodyStr.find("custom-value-123") != std::string::npos);
}
