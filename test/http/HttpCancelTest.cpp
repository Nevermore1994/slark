// Copyright (c) 2026 Nevermore-. All rights reserved.

#include "CancellationToken.h"
#include "HttpRequest.h"

#include <gtest/gtest.h>

#include <chrono>
#include <memory>
#include <thread>

using namespace slark;
using namespace slark::http;

namespace {

void resumeUntilDone(Task<void>& task, int32_t timeoutMs = 15000) {
    task.resume();
    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    while (!task.isDone() && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}

}  // namespace


TEST(HttpCancelTest, CancelDuringDnsPhase) {
    CancellationToken token;
    token.cancel();

    auto task = [&token]() -> Task<void> {
        RequestInfo info;
        info.url = "http://httpbingo.org/get";
        info.methodType = HttpMethodType::Get;
        info.connectTimeout = std::chrono::milliseconds(3000);
        info.readTimeout = std::chrono::milliseconds(10000);

        HttpRequest request;
        auto response = co_await request.asyncExecute(info, token);

        EXPECT_FALSE(response.isSuccess());
        EXPECT_EQ(response.error.retCode, slark::Error::Cancelled);
    }();

    auto start = std::chrono::steady_clock::now();
    resumeUntilDone(task);
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                       std::chrono::steady_clock::now() - start)
                       .count();
    EXPECT_TRUE(task.isDone());
    EXPECT_LT(elapsed, 2000) << "a cancelled token must not start any network request";
}

TEST(HttpCancelTest, CancelDuringTcpConnectPhase) {
    CancellationToken token;

    auto task = [&token]() -> Task<void> {
        RequestInfo info;
        info.url = "http://httpbingo.org/delay/5";
        info.methodType = HttpMethodType::Get;
        info.connectTimeout = std::chrono::milliseconds(3000);
        info.readTimeout = std::chrono::milliseconds(10000);

        HttpRequest request;
        auto response = co_await request.asyncExecute(info, token);

        EXPECT_FALSE(response.isSuccess());
        EXPECT_EQ(response.error.retCode, slark::Error::Cancelled);
    }();

    std::thread canceller([&token]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        token.cancel();
    });

    auto start = std::chrono::steady_clock::now();
    resumeUntilDone(task);
    canceller.join();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                       std::chrono::steady_clock::now() - start)
                       .count();
    EXPECT_TRUE(task.isDone());
    EXPECT_LT(elapsed, 2000) << "cancellation must penetrate TCP connect and return immediately";
}

TEST(HttpCancelTest, HttpRequestCancelCancelsExplicitTokenRequest) {
    CancellationToken externalToken;
    HttpRequest request;

    RequestInfo info;
    info.url = "http://httpbingo.org/delay/5";
    info.methodType = HttpMethodType::Get;
    info.connectTimeout = std::chrono::milliseconds(3000);
    info.readTimeout = std::chrono::milliseconds(10000);

    auto requestTask = request.asyncExecute(info, externalToken);
    auto task = [&]() -> Task<void> {
        auto response = co_await std::move(requestTask);
        EXPECT_EQ(response.error.retCode, slark::Error::Cancelled);
        EXPECT_FALSE(externalToken.isCancelled());
    }();

    std::thread canceller([&request]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        request.cancel();
    });

    resumeUntilDone(task);
    canceller.join();
    EXPECT_TRUE(task.isDone());
}

TEST(HttpCancelTest, CancelledRequestCanBeReused) {
    HttpRequest request;
    RequestInfo info;
    info.url = "http://httpbingo.org/delay/5";
    info.methodType = HttpMethodType::Get;
    info.connectTimeout = std::chrono::milliseconds(3000);
    info.readTimeout = std::chrono::milliseconds(10000);

    auto firstRequest = request.asyncExecute(info);
    request.cancel();

    auto task = [&]() -> Task<void> {
        auto firstResponse = co_await std::move(firstRequest);
        EXPECT_EQ(firstResponse.error.retCode, slark::Error::Cancelled);

        RequestInfo invalidInfo;
        invalidInfo.url = "not-a-url";
        invalidInfo.methodType = HttpMethodType::Get;
        auto secondResponse = co_await request.asyncExecute(invalidInfo);
        EXPECT_EQ(secondResponse.error.retCode, slark::Error::InvalidArgument);
    }();

    resumeUntilDone(task);
    EXPECT_TRUE(task.isDone());
}

TEST(HttpCancelTest, CancelDuringTlsPhase) {
    CancellationToken token;

    auto task = [&token]() -> Task<void> {
        RequestInfo info;
        info.url = "https://httpbingo.org/delay/5";
        info.methodType = HttpMethodType::Get;
        info.connectTimeout = std::chrono::milliseconds(3000);
        info.readTimeout = std::chrono::milliseconds(10000);

        HttpRequest request;
        auto response = co_await request.asyncExecute(info, token);

        EXPECT_FALSE(response.isSuccess());
        EXPECT_EQ(response.error.retCode, slark::Error::Cancelled);
    }();

    std::thread canceller([&token]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        token.cancel();
    });

    auto start = std::chrono::steady_clock::now();
    resumeUntilDone(task);
    canceller.join();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                       std::chrono::steady_clock::now() - start)
                       .count();
    EXPECT_TRUE(task.isDone());
    EXPECT_LT(elapsed, 2000) << "cancellation must penetrate TLS handshake and return immediately";
}

TEST(HttpCancelTest, CancelAfterHeaderChunkStopsStream) {
    auto task = []() -> Task<void> {
        RequestInfo info;
        info.url = "http://httpbingo.org/stream-bytes/10240";
        info.methodType = HttpMethodType::Get;
        info.readTimeout = std::chrono::milliseconds(10000);

        HttpRequest request;
        auto gen = request.streamingExecute(info);

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
            if (chunkCount == 1) {
                request.cancel();
            }
        }

        EXPECT_LT(receivedBytes, 10240u);
    }();

    resumeUntilDone(task);
    EXPECT_TRUE(task.isDone());
}

TEST(HttpCancelTest, CancelAndReadTimeoutRaceIsSafe) {
    CancellationToken token;

    auto task = [&token]() -> Task<void> {
        RequestInfo info;
        info.url = "http://httpbingo.org/stream-bytes/10240";
        info.methodType = HttpMethodType::Get;
        info.readTimeout = std::chrono::milliseconds(200);

        HttpRequest request;
        auto response = co_await request.asyncExecute(info, token);

        EXPECT_TRUE(response.error.retCode == slark::Error::Cancelled ||
                    response.error.retCode == slark::Error::ConnectionTimeout);
    }();

    std::thread canceller([&token]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(120));
        token.cancel();
    });

    resumeUntilDone(task);
    EXPECT_TRUE(task.isDone());
    canceller.join();
}
