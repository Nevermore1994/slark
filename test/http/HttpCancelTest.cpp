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

// 手动推进 Task：resume 启动，轮询等待 IO 线程回调恢复协程
void resumeUntilDone(Task<void>& task, int32_t timeoutMs = 15000) {
    task.resume();
    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    while (!task.isDone() && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}

}  // namespace

// 取消协议测试：T1-T3 验证取消穿透 DNS/TCP/TLS（C-06），3.3 修复后启用。

TEST(HttpCancelTest, CancelDuringDnsPhase) {
    // 预取消 token：DNS 解析应跳过线程池立即返回 null，不进入后续流程
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
    EXPECT_LT(elapsed, 2000) << "已取消的 token 不应发起任何网络请求";
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
    EXPECT_LT(elapsed, 2000) << "取消应穿透 TCP connect 阶段立即返回";
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
    EXPECT_LT(elapsed, 2000) << "取消应穿透 TLS handshake 阶段立即返回";
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

        // 取消与超时竞争：结果只能是 Cancelled 或 ConnectionTimeout，不崩溃即可
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
