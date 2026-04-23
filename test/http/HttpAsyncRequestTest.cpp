//
// Created by Nevermore on 2026/4/7.
// Slark HTTP Async Request Tests
// Copyright (c) 2026 Nevermore All rights reserved.
//
#include "AsyncGenerator.hpp"
#include "CancellationToken.h"
#include "Coroutine.hpp"
#include "CoroutineScheduler.hpp"
#include "HttpRequest.h"
#include "ThreadPool.hpp"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <gtest/gtest.h>
#include <mutex>
#include <thread>

using namespace slark;
using namespace slark::http;

// HTTP 协程异步请求测试
TEST(HttpAsyncRequestTest, AsyncExecuteWithCoroutine) {
    auto coro = []() -> Coroutine<void> {
        RequestInfo info;
        info.url = "http://httpbin.org/get";
        info.methodType = HttpMethodType::Get;
        info.readTimeout = std::chrono::milliseconds(5000);

        HttpRequest request;
        auto response = co_await request.asyncExecute(info);

        EXPECT_TRUE(response.isSuccess());
        EXPECT_EQ(response.statusCode, HttpStatusCode::OK);
        EXPECT_TRUE(response.body != nullptr);
    }();

    coro.resume();
    EXPECT_TRUE(coro.isDone());
}

TEST(HttpAsyncRequestTest, AsyncExecuteWithPost) {
    auto coro = []() -> Coroutine<void> {
        RequestInfo info;
        info.url = "http://httpbin.org/post";
        info.methodType = HttpMethodType::Post;
        info.readTimeout = std::chrono::milliseconds(5000);
        info.headers["Content-Type"] = "application/json";
        info.body = std::make_shared<Data>(R"({"test": "data"})");

        HttpRequest request;
        auto response = co_await request.asyncExecute(info);

        EXPECT_TRUE(response.isSuccess());
        EXPECT_EQ(response.statusCode, HttpStatusCode::OK);
    }();

    coro.resume();
    EXPECT_TRUE(coro.isDone());
}

TEST(HttpAsyncRequestTest, AsyncExecuteWithCancellation) {
    CancellationToken token;

    auto coro = [&token]() -> Coroutine<void> {
        RequestInfo info;
        info.url = "http://httpbin.org/delay/5";
        info.methodType = HttpMethodType::Get;
        info.readTimeout = std::chrono::milliseconds(10000);

        HttpRequest request;
        auto response = co_await request.asyncExecute(info, token);

        EXPECT_FALSE(response.isSuccess());
        EXPECT_EQ(response.error.retCode, ResultCode::Cancelled);
    }();

    std::thread canceller([&token]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        token.cancel();
    });

    coro.resume();
    EXPECT_TRUE(coro.isDone());

    canceller.join();
}

TEST(HttpAsyncRequestTest, AsyncExecuteWithTimeout) {
    auto coro = []() -> Coroutine<void> {
        RequestInfo info;
        info.url = "http://httpbin.org/delay/10";
        info.methodType = HttpMethodType::Get;
        info.readTimeout = std::chrono::milliseconds(1000);

        HttpRequest request;
        auto response = co_await request.asyncExecute(info);

        EXPECT_FALSE(response.isSuccess());
        EXPECT_EQ(response.error.retCode, ResultCode::Timeout);
    }();

    coro.resume();
    EXPECT_TRUE(coro.isDone());
}

TEST(HttpAsyncRequestTest, AsyncExecuteInvalidUrl) {
    auto coro = []() -> Coroutine<void> {
        RequestInfo info;
        info.url = "not-a-valid-url";
        info.methodType = HttpMethodType::Get;
        info.readTimeout = std::chrono::milliseconds(1000);

        HttpRequest request;
        auto response = co_await request.asyncExecute(info);

        EXPECT_FALSE(response.isSuccess());
        EXPECT_NE(response.error.retCode, ResultCode::Success);
    }();

    coro.resume();
    EXPECT_TRUE(coro.isDone());
}

TEST(HttpAsyncRequestTest, AsyncExecuteHttps) {
    auto coro = []() -> Coroutine<void> {
        RequestInfo info;
        info.url = "https://httpbin.org/get";
        info.methodType = HttpMethodType::Get;
        info.readTimeout = std::chrono::milliseconds(5000);

        HttpRequest request;
        auto response = co_await request.asyncExecute(info);

        EXPECT_TRUE(response.isSuccess());
        EXPECT_EQ(response.statusCode, HttpStatusCode::OK);
    }();

    coro.resume();
    EXPECT_TRUE(coro.isDone());
}

// HTTP 回调异步请求测试
TEST(HttpAsyncRequestTest, CallbackExecute) {
    std::mutex mtx;
    std::condition_variable cv;
    bool completed = false;
    HttpResponse storedResponse;

    RequestInfo info;
    info.url = "http://httpbin.org/get";
    info.methodType = HttpMethodType::Get;
    info.readTimeout = std::chrono::milliseconds(5000);

    HttpRequest request;

    HttpResponseHandler handler;
    handler.onHeader = [&](const RequestInfo&, ResponseHeader&& header) {
        storedResponse.statusCode = header.httpStatusCode;
        storedResponse.headers = std::move(header.headers);
    };
    handler.onData = [&](const RequestInfo&, DataPtr data) {
        if (data) {
            if (storedResponse.body) {
                storedResponse.body->append(*data);
            } else {
                storedResponse.body = std::move(data);
            }
        }
    };
    handler.onCompleted = [&](const RequestInfo&) {
        std::lock_guard lock(mtx);
        completed = true;
        cv.notify_one();
    };
    handler.onError = [&](const RequestInfo&, ErrorInfo error) {
        std::lock_guard lock(mtx);
        storedResponse.error = error;
        completed = true;
        cv.notify_one();
    };

    request.asyncExecute(info, std::move(handler));

    std::unique_lock lock(mtx);
    bool result = cv.wait_for(lock, std::chrono::seconds(10), [&] {
        return completed;
    });
    EXPECT_TRUE(result);
    EXPECT_EQ(storedResponse.statusCode, HttpStatusCode::OK);
}

TEST(HttpAsyncRequestTest, CallbackExecuteWithCreate) {
    std::mutex mtx;
    std::condition_variable cv;
    bool completed = false;
    HttpResponse storedResponse;

    RequestInfo info;
    info.url = "http://httpbin.org/get";
    info.methodType = HttpMethodType::Get;
    info.readTimeout = std::chrono::milliseconds(5000);

    HttpResponseHandler handler;
    handler.onHeader = [&](const RequestInfo&, ResponseHeader&& header) {
        storedResponse.statusCode = header.httpStatusCode;
    };
    handler.onData = [&](const RequestInfo&, DataPtr data) {
        if (data) {
            storedResponse.body = std::move(data);
        }
    };
    handler.onCompleted = [&](const RequestInfo&) {
        std::lock_guard lock(mtx);
        completed = true;
        cv.notify_one();
    };
    handler.onError = [&](const RequestInfo&, ErrorInfo error) {
        std::lock_guard lock(mtx);
        storedResponse.error = error;
        completed = true;
        cv.notify_one();
    };

    auto request = HttpRequest::create(info, std::move(handler));

    EXPECT_NE(request, nullptr);

    std::unique_lock lock(mtx);
    bool result = cv.wait_for(lock, std::chrono::seconds(10), [&] {
        return completed;
    });
    EXPECT_TRUE(result);
    EXPECT_EQ(storedResponse.statusCode, HttpStatusCode::OK);
}

TEST(HttpAsyncRequestTest, CallbackExecuteWithCancel) {
    std::mutex mtx;
    std::condition_variable cv;
    bool completed = false;
    HttpResponse storedResponse;

    RequestInfo info;
    info.url = "http://httpbin.org/delay/5";
    info.methodType = HttpMethodType::Get;
    info.readTimeout = std::chrono::milliseconds(10000);

    HttpResponseHandler handler;
    handler.onCompleted = [&](const RequestInfo&) {
        std::lock_guard lock(mtx);
        completed = true;
        cv.notify_one();
    };
    handler.onError = [&](const RequestInfo&, ErrorInfo error) {
        std::lock_guard lock(mtx);
        storedResponse.error = error;
        completed = true;
        cv.notify_one();
    };

    auto request = HttpRequest::create(info, std::move(handler));

    EXPECT_NE(request, nullptr);

    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    request->cancel();

    std::unique_lock lock(mtx);
    bool result = cv.wait_for(lock, std::chrono::seconds(10), [&] {
        return completed;
    });
    EXPECT_TRUE(result);
    EXPECT_EQ(storedResponse.error.retCode, ResultCode::Cancelled);
}

// HTTP 流式请求测试
TEST(HttpAsyncRequestTest, StreamingExecute) {
    auto coro = []() -> Coroutine<void> {
        RequestInfo info;
        info.url = "http://httpbin.org/stream-bytes/1024";
        info.methodType = HttpMethodType::Get;
        info.readTimeout = std::chrono::milliseconds(10000);

        HttpRequest request;
        auto gen = request.streamingExecute(info);

        size_t totalBytes = 0;
        int32_t chunkCount = 0;

        while (true) {
            auto chunk = co_await gen.next();
            if (!chunk.has_value()) {
                break;
            }
            if (chunk->hasData()) {
                totalBytes += chunk->data->length();
            }
            chunkCount++;
        }

        EXPECT_GT(chunkCount, 0);
        EXPECT_EQ(totalBytes, 1024u);
    }();

    coro.resume();
    EXPECT_TRUE(coro.isDone());
}

TEST(HttpAsyncRequestTest, StreamingExecuteWithCancellation) {
    CancellationToken token;

    auto coro = [&token]() -> Coroutine<void> {
        RequestInfo info;
        info.url = "http://httpbin.org/stream-bytes/10240";
        info.methodType = HttpMethodType::Get;
        info.readTimeout = std::chrono::milliseconds(10000);

        HttpRequest request;
        auto gen = request.streamingExecute(info, token);

        size_t receivedBytes = 0;
        int32_t chunkCount = 0;

        while (true) {
            auto chunk = co_await gen.next();
            if (!chunk.has_value()) {
                break;
            }
            if (chunk->hasData()) {
                receivedBytes += chunk->data->length();
            }
            chunkCount++;

            if (chunkCount >= 2) {
                token.cancel();
            }
        }

        EXPECT_LT(receivedBytes, 10240u);
    }();

    coro.resume();
    EXPECT_TRUE(coro.isDone());
}

// 并发请求测试
TEST(HttpAsyncRequestTest, ConcurrentRequests) {
    ThreadPool pool(ThreadPoolConfig{4});
    std::atomic<int32_t> successCount{0};
    std::atomic<int32_t> failCount{0};

    constexpr int32_t kRequestCount = 5;

    for (int32_t i = 0; i < kRequestCount; ++i) {
        pool.postJob([&successCount, &failCount]() {
            auto coro = [&successCount, &failCount]() -> Coroutine<void> {
                RequestInfo info;
                info.url = "http://httpbin.org/get";
                info.methodType = HttpMethodType::Get;
                info.readTimeout = std::chrono::milliseconds(10000);

                HttpRequest request;
                auto response = co_await request.asyncExecute(info);

                if (response.isSuccess()) {
                    successCount++;
                } else {
                    failCount++;
                }
            }();

            coro.resume();
        });
    }

    pool.waitForAll();

    EXPECT_EQ(successCount.load() + failCount.load(), kRequestCount);
    EXPECT_GT(successCount.load(), 0);
}

// 重定向测试
TEST(HttpAsyncRequestTest, RedirectFollowing) {
    auto coro = []() -> Coroutine<void> {
        RequestInfo info;
        info.url = "http://httpbin.org/redirect/2";
        info.methodType = HttpMethodType::Get;
        info.readTimeout = std::chrono::milliseconds(5000);
        info.isAllowRedirect = true;

        HttpRequest request;
        auto response = co_await request.asyncExecute(info);

        EXPECT_TRUE(response.isSuccess());
        EXPECT_EQ(response.statusCode, HttpStatusCode::OK);
    }();

    coro.resume();
    EXPECT_TRUE(coro.isDone());
}

TEST(HttpAsyncRequestTest, RedirectDisabled) {
    auto coro = []() -> Coroutine<void> {
        RequestInfo info;
        info.url = "http://httpbin.org/redirect/1";
        info.methodType = HttpMethodType::Get;
        info.readTimeout = std::chrono::milliseconds(5000);
        info.isAllowRedirect = false;

        HttpRequest request;
        auto response = co_await request.asyncExecute(info);

        EXPECT_TRUE(response.isNeedRedirect());
    }();

    coro.resume();
    EXPECT_TRUE(coro.isDone());
}

// 自定义头部测试
TEST(HttpAsyncRequestTest, CustomHeaders) {
    auto coro = []() -> Coroutine<void> {
        RequestInfo info;
        info.url = "http://httpbin.org/headers";
        info.methodType = HttpMethodType::Get;
        info.readTimeout = std::chrono::milliseconds(5000);
        info.headers["X-Custom-Header"] = "TestValue";
        info.headers["X-Another-Header"] = "AnotherValue";

        HttpRequest request;
        auto response = co_await request.asyncExecute(info);

        EXPECT_TRUE(response.isSuccess());
        EXPECT_EQ(response.statusCode, HttpStatusCode::OK);

        if (response.body) {
            std::string bodyStr(reinterpret_cast<const char*>(response.body->data()),
                                response.body->length());
            EXPECT_TRUE(bodyStr.find("X-Custom-Header") != std::string::npos);
            EXPECT_TRUE(bodyStr.find("TestValue") != std::string::npos);
        }
    }();

    coro.resume();
    EXPECT_TRUE(coro.isDone());
}

// User-Agent 测试
TEST(HttpAsyncRequestTest, UserAgent) {
    auto coro = []() -> Coroutine<void> {
        RequestInfo info;
        info.url = "http://httpbin.org/user-agent";
        info.methodType = HttpMethodType::Get;
        info.readTimeout = std::chrono::milliseconds(5000);
        info.headers["User-Agent"] = "SlarkTest/1.0";

        HttpRequest request;
        auto response = co_await request.asyncExecute(info);

        EXPECT_TRUE(response.isSuccess());
        EXPECT_EQ(response.statusCode, HttpStatusCode::OK);

        if (response.body) {
            std::string bodyStr(reinterpret_cast<const char*>(response.body->data()),
                                response.body->length());
            EXPECT_TRUE(bodyStr.find("SlarkTest/1.0") != std::string::npos);
        }
    }();

    coro.resume();
    EXPECT_TRUE(coro.isDone());
}

// 不同 HTTP 方法测试
TEST(HttpAsyncRequestTest, PutMethod) {
    auto coro = []() -> Coroutine<void> {
        RequestInfo info;
        info.url = "http://httpbin.org/put";
        info.methodType = HttpMethodType::Put;
        info.readTimeout = std::chrono::milliseconds(5000);
        info.headers["Content-Type"] = "application/json";
        info.body = std::make_shared<Data>(R"({"update": "data"})");

        HttpRequest request;
        auto response = co_await request.asyncExecute(info);

        EXPECT_TRUE(response.isSuccess());
        EXPECT_EQ(response.statusCode, HttpStatusCode::OK);
    }();

    coro.resume();
    EXPECT_TRUE(coro.isDone());
}

TEST(HttpAsyncRequestTest, PatchMethod) {
    auto coro = []() -> Coroutine<void> {
        RequestInfo info;
        info.url = "http://httpbin.org/patch";
        info.methodType = HttpMethodType::Patch;
        info.readTimeout = std::chrono::milliseconds(5000);
        info.headers["Content-Type"] = "application/json";
        info.body = std::make_shared<Data>(R"({"patch": "data"})");

        HttpRequest request;
        auto response = co_await request.asyncExecute(info);

        EXPECT_TRUE(response.isSuccess());
        EXPECT_EQ(response.statusCode, HttpStatusCode::OK);
    }();

    coro.resume();
    EXPECT_TRUE(coro.isDone());
}

TEST(HttpAsyncRequestTest, DeleteMethod) {
    auto coro = []() -> Coroutine<void> {
        RequestInfo info;
        info.url = "http://httpbin.org/delete";
        info.methodType = HttpMethodType::Delete;
        info.readTimeout = std::chrono::milliseconds(5000);

        HttpRequest request;
        auto response = co_await request.asyncExecute(info);

        EXPECT_TRUE(response.isSuccess());
        EXPECT_EQ(response.statusCode, HttpStatusCode::OK);
    }();

    coro.resume();
    EXPECT_TRUE(coro.isDone());
}

// 状态码测试
TEST(HttpAsyncRequestTest, NotFoundStatus) {
    auto coro = []() -> Coroutine<void> {
        RequestInfo info;
        info.url = "http://httpbin.org/status/404";
        info.methodType = HttpMethodType::Get;
        info.readTimeout = std::chrono::milliseconds(5000);

        HttpRequest request;
        auto response = co_await request.asyncExecute(info);

        EXPECT_EQ(response.statusCode, HttpStatusCode::NotFound);
    }();

    coro.resume();
    EXPECT_TRUE(coro.isDone());
}

TEST(HttpAsyncRequestTest, ServerErrorStatus) {
    auto coro = []() -> Coroutine<void> {
        RequestInfo info;
        info.url = "http://httpbin.org/status/500";
        info.methodType = HttpMethodType::Get;
        info.readTimeout = std::chrono::milliseconds(5000);

        HttpRequest request;
        auto response = co_await request.asyncExecute(info);

        EXPECT_EQ(response.statusCode, HttpStatusCode::InternalServerError);
    }();

    coro.resume();
    EXPECT_TRUE(coro.isDone());
}

// 集成测试：协程调度器与HTTP请求
TEST(HttpAsyncRequestIntegrationTest, ScheduledHttpRequest) {
    auto& scheduler = CoroutineScheduler::instance();

    std::atomic<bool> completed{false};

    auto coro = [&]() -> Coroutine<void> {
        RequestInfo info;
        info.url = "http://httpbin.org/get";
        info.methodType = HttpMethodType::Get;
        info.readTimeout = std::chrono::milliseconds(5000);

        HttpRequest request;
        auto response = co_await request.asyncExecute(info);

        EXPECT_TRUE(response.isSuccess());
        completed = true;
    }();

    scheduler.schedule(coro.getHandle());

    int32_t waitCount = 0;
    while (!completed.load() && waitCount < 100) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        waitCount++;
    }

    EXPECT_TRUE(completed.load());
}
