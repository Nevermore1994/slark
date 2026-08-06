#include "HttpProcessor.h"

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <memory>

#include "CancellationToken.h"
#include "ThreadPool.hpp"

using namespace slark;
using namespace slark::http;

namespace {

HttpResponse executeSync(const RequestInfo& info) {
    ThreadPool pool(ThreadPoolConfig{2});
    std::atomic<bool> done{false};
    HttpResponse result;

    auto task = [&]() -> Task<void> {
        HttpProcessor processor;
        auto r = co_await processor.process(info);
        if (r.has_value()) {
            result = std::move(r.value());
        } else {
            result.error = r.error();
        }
        done = true;
    }();
    task.resume();
    for (int i = 0; i < 200 && !done.load(); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    EXPECT_TRUE(done.load()) << "Request timed out: " << info.url;
    return result;
}

}  // namespace

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
    std::atomic<bool> done{false};
    HttpResponse result;
    auto task = [&]() -> Task<void> {
        HttpProcessor processor;
        auto r = co_await processor.process(info, token);
        if (r.has_value()) {
            result = std::move(r.value());
        } else {
            result.error = r.error();
        }
        done = true;
    }();
    task.resume();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    token.cancel();
    for (int i = 0; i < 200 && !done.load(); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    EXPECT_TRUE(done.load());
    EXPECT_EQ(result.error.retCode, Error::Cancelled);
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
    std::atomic<bool> done{false};
    Error error = Error::Success;
    auto task = [&]() -> Task<void> {
        HttpProcessor processor;
        auto gen = processor.processStreaming(info);
        while (true) {
            auto chunk = co_await gen.next();
            if (!chunk.has_value()) {
                break;
            }
            if (chunk->hasError()) {
                error = chunk->error.retCode;
            }
        }
        done = true;
    }();
    task.resume();
    for (int i = 0; i < 200 && !done.load(); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    EXPECT_TRUE(done.load());
    EXPECT_EQ(error, Error::HttpResponseBodyTooLarge);
}

TEST(HttpProcessorTest, StreamingGetReturnsChunks) {
    RequestInfo info;
    info.url = "http://httpbingo.org/stream-bytes/1024";
    info.methodType = HttpMethodType::Get;
    info.connectTimeout = std::chrono::seconds(5);
    info.readTimeout = std::chrono::seconds(15);
    ThreadPool pool(ThreadPoolConfig{2});
    std::atomic<bool> done{false};
    size_t totalBytes = 0;
    int32_t chunkCount = 0;
    auto task = [&]() -> Task<void> {
        HttpProcessor processor;
        auto gen = processor.processStreaming(info);
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
        done = true;
    }();
    task.resume();
    for (int i = 0; i < 200 && !done.load(); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    EXPECT_TRUE(done.load());
    EXPECT_GT(chunkCount, 0);
    EXPECT_EQ(totalBytes, 1024u);
}

TEST(HttpProcessorTest, StreamingFollowsRedirect) {
    RequestInfo info;
    info.url = "http://httpbingo.org/redirect-to?url=/get&status_code=302";
    info.methodType = HttpMethodType::Get;
    info.connectTimeout = std::chrono::seconds(5);
    info.readTimeout = std::chrono::seconds(15);
    info.isAllowRedirect = true;
    ThreadPool pool(ThreadPoolConfig{2});
    std::atomic<bool> done{false};
    HttpStatusCode lastStatus = HttpStatusCode::Unknown;
    size_t totalBytes = 0;
    int32_t chunkCount = 0;
    auto task = [&]() -> Task<void> {
        HttpProcessor processor;
        auto gen = processor.processStreaming(info);
        while (true) {
            auto chunk = co_await gen.next();
            if (!chunk.has_value()) {
                break;
            }
            if (chunk->statusCode != HttpStatusCode::Unknown) {
                lastStatus = chunk->statusCode;
            }
            if (chunk->hasData()) {
                totalBytes += chunk->data->length();
            }
            chunkCount++;
        }
        done = true;
    }();
    task.resume();
    for (int i = 0; i < 200 && !done.load(); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    EXPECT_TRUE(done.load());
    EXPECT_EQ(lastStatus, HttpStatusCode::OK);
    EXPECT_GT(totalBytes, 0u);
    EXPECT_GT(chunkCount, 0);
}
