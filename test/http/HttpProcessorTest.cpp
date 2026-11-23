#include "HttpProcessor.h"

#include <gtest/gtest.h>

#include <chrono>
#include <memory>
#include <thread>
#include <vector>

#include "CancellationToken.h"
#include "ThreadPool.hpp"

using namespace slark;
using namespace slark::http;

namespace {

template <typename Func>
Task<void> runVoidTask(Func func) {
    co_await func();
}

// A timed-out chain is adopted here instead of being destroyed on the test thread,
// which could race with a child frame still in await_suspend on the EventLoop thread.
std::vector<Task<void>>& orphanedFrames() {
    static auto* frames = new std::vector<Task<void>>;
    return *frames;
}

// isDone() is set by final_suspend/finish(), after the whole chain has settled.
// A body-local flag fires earlier, before co_return and final_suspend.
bool waitForDone(Task<void>& task, std::chrono::milliseconds budget) {
    const auto deadline = std::chrono::steady_clock::now() + budget;
    while (!task.isDone() && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    if (!task.isDone()) {
        orphanedFrames().push_back(std::move(task));
        return false;
    }
    return true;
}

// Cover connect + read timeouts plus headroom; the old 2s budget was shorter than
// the requests' own 5s/10s timeouts, so a slow response returned and destroyed a live chain.
std::chrono::milliseconds syncBudget(const RequestInfo& info) {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        info.connectTimeout + info.readTimeout + std::chrono::seconds(5));
}

template <typename Func>
bool runSync(Func func, std::chrono::milliseconds budget) {
    auto task = runVoidTask(std::move(func));
    task.resume();
    return waitForDone(task, budget);
}

struct StreamOutcome {
    Error error = Error::Success;
    size_t totalBytes = 0;
    int32_t chunkCount = 0;
    HttpStatusCode lastStatus = HttpStatusCode::Unknown;
};

HttpResponse executeSync(const RequestInfo& info) {
    // Shared so a timed-out body adopted by orphanedFrames() never touches a dead stack object.
    auto result = std::make_shared<HttpResponse>();
    const bool settled = runSync(
        [result, info]() -> Task<void> {
            HttpProcessor processor;
            auto r = co_await processor.process(info);
            if (r.has_value()) {
                *result = std::move(r.value());
            } else {
                result->error = r.error();
            }
        },
        syncBudget(info));
    if (!settled) {
        ADD_FAILURE() << "Request timed out: " << info.url;
        HttpResponse timeout;
        timeout.error = ErrorInfo{Error::Failed};
        return timeout;
    }
    return std::move(*result);
}

}  // namespace

TEST(HttpProcessorTest, NormalizeTransportErrorPreservesSpecificCodes) {
    EXPECT_EQ(normalizeTransportError(Error::ConnectionTimeout), Error::ConnectionTimeout);
    EXPECT_EQ(normalizeTransportError(Error::ConnectionRefused), Error::ConnectionRefused);
    EXPECT_EQ(normalizeTransportError(Error::HttpSslError), Error::HttpSslError);
    EXPECT_EQ(normalizeTransportError(Error::Cancelled), Error::Cancelled);
    EXPECT_EQ(normalizeTransportError(Error::Failed), Error::NetworkError);
}

TEST(HttpProcessorTest, ProcessGetReturnsSuccess) {
    RequestInfo info;
    info.url = "http://httpbingo.org/get";
    info.methodType = HttpMethodType::Get;
    info.connectTimeout = std::chrono::seconds(5);
    info.readTimeout = std::chrono::seconds(10);
    auto response = executeSync(info);
    EXPECT_EQ(response.error.retCode, Error::Success);
    EXPECT_EQ(response.statusCode, HttpStatusCode::OK);
}

TEST(HttpProcessorTest, ProcessPostWithBody) {
    RequestInfo info;
    info.url = "http://httpbingo.org/post";
    info.methodType = HttpMethodType::Post;
    info.headers["Content-Type"] = "application/json";
    info.body = std::make_shared<Data>(R"({"test": "data"})");
    info.connectTimeout = std::chrono::seconds(5);
    info.readTimeout = std::chrono::seconds(10);
    auto response = executeSync(info);
    EXPECT_EQ(response.error.retCode, Error::Success);
    EXPECT_EQ(response.statusCode, HttpStatusCode::OK);
}

TEST(HttpProcessorTest, ProcessReturns404) {
    RequestInfo info;
    info.url = "http://httpbingo.org/status/404";
    info.methodType = HttpMethodType::Get;
    info.connectTimeout = std::chrono::seconds(5);
    info.readTimeout = std::chrono::seconds(10);
    auto response = executeSync(info);
    EXPECT_EQ(response.statusCode, HttpStatusCode::NotFound);
    EXPECT_EQ(response.error.retCode, Error::HttpError);
}

TEST(HttpProcessorTest, ProcessPropagatesDnsError) {
    RequestInfo info;
    info.url = "http://this.host.does.not.exist.invalid/";
    info.methodType = HttpMethodType::Get;
    info.connectTimeout = std::chrono::milliseconds(500);
    info.readTimeout = std::chrono::milliseconds(500);

    const auto response = executeSync(info);

    EXPECT_EQ(response.error.retCode, Error::DnsResolveFailed);
}

TEST(HttpProcessorTest, ProcessRejectsResponseBodyAboveLimit) {
    RequestInfo info;
    info.url = "http://httpbingo.org/stream-bytes/1024";
    info.methodType = HttpMethodType::Get;
    info.connectTimeout = std::chrono::seconds(5);
    info.readTimeout = std::chrono::seconds(10);
    info.maxResponseBodySize = 128;

    auto response = executeSync(info);
    EXPECT_EQ(response.error.retCode, Error::HttpResponseBodyTooLarge);
    EXPECT_EQ(response.body, nullptr);
}

TEST(HttpProcessorTest, ProcessCancelledDuringRequest) {
    CancellationToken token;
    RequestInfo info;
    info.url = "http://httpbingo.org/delay/5";
    info.methodType = HttpMethodType::Get;
    info.connectTimeout = std::chrono::seconds(5);
    info.readTimeout = std::chrono::seconds(15);
    ThreadPool pool(ThreadPoolConfig{2});
    auto result = std::make_shared<HttpResponse>();
    auto task = runVoidTask([result, info, token]() -> Task<void> {
        HttpProcessor processor;
        auto r = co_await processor.process(info, token);
        if (r.has_value()) {
            *result = std::move(r.value());
        } else {
            result->error = r.error();
        }
    });
    task.resume();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    token.cancel();
    ASSERT_TRUE(waitForDone(task, syncBudget(info)));
    EXPECT_EQ(result->error.retCode, Error::Cancelled);
}

TEST(HttpProcessorTest, ProcessTimeout) {
    RequestInfo info;
    info.url = "http://httpbingo.org/delay/10";
    info.methodType = HttpMethodType::Get;
    info.connectTimeout = std::chrono::seconds(5);
    info.readTimeout = std::chrono::milliseconds(500);
    auto response = executeSync(info);
    EXPECT_EQ(response.error.retCode, Error::ConnectionTimeout);
}

TEST(HttpProcessorTest, StreamingRejectsResponseBodyAboveLimit) {
    RequestInfo info;
    info.url = "http://httpbingo.org/stream-bytes/1024";
    info.methodType = HttpMethodType::Get;
    info.connectTimeout = std::chrono::seconds(5);
    info.readTimeout = std::chrono::seconds(10);
    info.maxResponseBodySize = 128;

    ThreadPool pool(ThreadPoolConfig{2});
    auto outcome = std::make_shared<StreamOutcome>();
    ASSERT_TRUE(runSync(
        [outcome, info]() -> Task<void> {
            HttpProcessor processor;
            auto gen = processor.processStreaming(info);
            while (true) {
                auto chunk = co_await gen.next();
                if (!chunk.has_value()) {
                    break;
                }
                if (chunk->hasError()) {
                    outcome->error = chunk->error.retCode;
                }
            }
        },
        syncBudget(info)));
    EXPECT_EQ(outcome->error, Error::HttpResponseBodyTooLarge);
}

TEST(HttpProcessorTest, StreamingGetReturnsChunks) {
    RequestInfo info;
    info.url = "http://httpbingo.org/stream-bytes/1024";
    info.methodType = HttpMethodType::Get;
    info.connectTimeout = std::chrono::seconds(5);
    info.readTimeout = std::chrono::seconds(15);
    ThreadPool pool(ThreadPoolConfig{2});
    auto outcome = std::make_shared<StreamOutcome>();
    ASSERT_TRUE(runSync(
        [outcome, info]() -> Task<void> {
            HttpProcessor processor;
            auto gen = processor.processStreaming(info);
            while (true) {
                auto chunk = co_await gen.next();
                if (!chunk.has_value()) {
                    break;
                }
                if (chunk->hasData()) {
                    outcome->totalBytes += chunk->data->length();
                }
                ++outcome->chunkCount;
            }
        },
        syncBudget(info)));
    EXPECT_GT(outcome->chunkCount, 0);
    EXPECT_EQ(outcome->totalBytes, 1024u);
}

TEST(HttpProcessorTest, StreamingFollowsRedirect) {
    RequestInfo info;
    info.url = "http://httpbingo.org/redirect-to?url=/get&status_code=302";
    info.methodType = HttpMethodType::Get;
    info.connectTimeout = std::chrono::seconds(5);
    info.readTimeout = std::chrono::seconds(15);
    info.isAllowRedirect = true;
    ThreadPool pool(ThreadPoolConfig{2});
    auto outcome = std::make_shared<StreamOutcome>();
    ASSERT_TRUE(runSync(
        [outcome, info]() -> Task<void> {
            HttpProcessor processor;
            auto gen = processor.processStreaming(info);
            while (true) {
                auto chunk = co_await gen.next();
                if (!chunk.has_value()) {
                    break;
                }
                if (chunk->statusCode != HttpStatusCode::Unknown) {
                    outcome->lastStatus = chunk->statusCode;
                }
                if (chunk->hasData()) {
                    outcome->totalBytes += chunk->data->length();
                }
                ++outcome->chunkCount;
            }
        },
        syncBudget(info)));
    EXPECT_EQ(outcome->lastStatus, HttpStatusCode::OK);
    EXPECT_GT(outcome->totalBytes, 0u);
    EXPECT_GT(outcome->chunkCount, 0);
}
