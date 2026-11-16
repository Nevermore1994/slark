//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//
#include <gtest/gtest.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <array>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include "Socket.hpp"

#include "AsyncGenerator.hpp"
#include "CancellationToken.h"
#include "HttpRequest.h"

using namespace slark;
using namespace slark::http;

namespace {

template <typename Func>
Task<void> runVoidTask(Func func) {
    co_await func();
}

void resumeAndWait(Task<void>& coro) {
    coro.resume();
    for (int32_t i = 0; i < 200 && !coro.isDone(); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
}

constexpr int kStopPollMs = 20;

class StreamingCancellationServer final {
 public:
    StreamingCancellationServer() {
        listenFd_ = ::socket(AF_INET, SOCK_STREAM, 0);
        if (listenFd_ < 0) {
            throw std::runtime_error("failed to create cancellation test socket");
        }
        int reuse = 1;
        ::setsockopt(listenFd_, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        address.sin_port = 0;
        if (::bind(listenFd_, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) < 0 ||
            ::listen(listenFd_, 1) < 0) {
            ::close(listenFd_);
            throw std::runtime_error("failed to bind cancellation test socket");
        }
        socklen_t length = sizeof(address);
        if (::getsockname(listenFd_, reinterpret_cast<sockaddr*>(&address), &length) < 0) {
            ::close(listenFd_);
            throw std::runtime_error("failed to read cancellation test port");
        }
        port_ = ntohs(address.sin_port);
        worker_ = std::thread([this] { serve(); });
    }

    ~StreamingCancellationServer() {
        stopped_.store(true, std::memory_order_release);
        if (worker_.joinable()) {
            worker_.join();
        }
        if (listenFd_ >= 0) {
            ::close(listenFd_);
        }
    }

    StreamingCancellationServer(const StreamingCancellationServer&) = delete;
    StreamingCancellationServer& operator=(const StreamingCancellationServer&) = delete;

    [[nodiscard]] std::string url() const {
        return "http://127.0.0.1:" + std::to_string(port_) + "/stream";
    }

    [[nodiscard]] bool clientDisconnected() const noexcept {
        return clientDisconnected_.load(std::memory_order_acquire);
    }

 private:
    static bool sendAll(int fd, std::string_view data) noexcept {
        size_t sent = 0;
        while (sent < data.size()) {
            const auto count = ::send(fd, data.data() + sent, data.size() - sent, kNoSignal);
            if (count <= 0) {
                return false;
            }
            sent += static_cast<size_t>(count);
        }
        return true;
    }

    bool waitForClientClose(int fd) noexcept {
        pollfd event{fd, POLLIN, 0};
        const auto ready = ::poll(&event, 1, 3000);
        if (ready <= 0) {
            return false;
        }
        char byte = 0;
        const auto count = ::recv(fd, &byte, 1, MSG_DONTWAIT);
        return count == 0;
    }

    bool waitForClient() const noexcept {
        while (!stopped_.load(std::memory_order_acquire)) {
            pollfd event{listenFd_, POLLIN, 0};
            if (::poll(&event, 1, kStopPollMs) > 0) {
                return true;
            }
        }
        return false;
    }

    void serve() noexcept {
        if (!waitForClient()) {
            return;
        }
        const int client = ::accept(listenFd_, nullptr, nullptr);
        if (client < 0) {
            return;
        }
#if defined(__APPLE__)
        int noSigpipe = 1;
        ::setsockopt(client, SOL_SOCKET, SO_NOSIGPIPE, &noSigpipe, sizeof(noSigpipe));
#endif
        std::string request;
        std::array<char, 1024> buffer{};
        while (!stopped_.load(std::memory_order_acquire) &&
               request.find("\r\n\r\n") == std::string::npos) {
            const auto count = ::recv(client, buffer.data(), buffer.size(), 0);
            if (count <= 0) {
                ::close(client);
                return;
            }
            request.append(buffer.data(), static_cast<size_t>(count));
        }

        constexpr size_t kFirstChunkSize = 1024;
        constexpr size_t kTotalBodySize = 10 * 1024;
        const std::string responseHeaders =
            "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\nConnection: close\r\n\r\n";
        const std::string firstChunk = "400\r\n" + std::string(kFirstChunkSize, 'a') + "\r\n";
        if (sendAll(client, responseHeaders) && sendAll(client, firstChunk)) {
            // Keep later chunks unavailable until cancellation closes the connection.
            if (waitForClientClose(client)) {
                clientDisconnected_.store(true, std::memory_order_release);
            } else {
                constexpr size_t kFollowupChunkSize = 1024;
                const std::string followupChunk =
                    "400\r\n" + std::string(kFollowupChunkSize, 'b') + "\r\n";
                size_t remaining = kTotalBodySize - kFirstChunkSize;
                while (remaining > 0 && !stopped_.load(std::memory_order_acquire)) {
                    if (!sendAll(client, followupChunk)) {
                        break;
                    }
                    remaining -= kFollowupChunkSize;
                    std::this_thread::sleep_for(std::chrono::milliseconds(2));
                }
                sendAll(client, "0\r\n\r\n");
            }
        }
        ::shutdown(client, SHUT_RDWR);
        ::close(client);
    }

    int listenFd_ = -1;
    uint16_t port_ = 0;
    std::atomic<bool> stopped_{false};
    std::atomic<bool> clientDisconnected_{false};
    std::thread worker_;
};

}  // namespace

TEST(HttpAsyncRequestTest, AsyncExecuteWithCoroutine) {
    auto coro = []() -> Task<void> {
        RequestInfo info;
        info.url = "http://httpbingo.org/get";
        info.methodType = HttpMethodType::Get;
        info.readTimeout = std::chrono::milliseconds(5000);

        HttpRequest request;
        auto response = co_await request.asyncExecute(info);

        EXPECT_TRUE(response.isSuccess());
        EXPECT_EQ(response.statusCode, HttpStatusCode::OK);
        EXPECT_TRUE(response.body != nullptr);
    }();

    resumeAndWait(coro);
    EXPECT_TRUE(coro.isDone());
}

TEST(HttpAsyncRequestTest, AsyncExecuteWithPost) {
    auto coro = []() -> Task<void> {
        RequestInfo info;
        info.url = "http://httpbingo.org/post";
        info.methodType = HttpMethodType::Post;
        info.readTimeout = std::chrono::milliseconds(5000);
        info.headers["Content-Type"] = "application/json";
        info.body = std::make_shared<Data>(R"({"test": "data"})");

        HttpRequest request;
        auto response = co_await request.asyncExecute(info);

        EXPECT_TRUE(response.isSuccess());
        EXPECT_EQ(response.statusCode, HttpStatusCode::OK);
    }();

    resumeAndWait(coro);
    EXPECT_TRUE(coro.isDone());
}

TEST(HttpAsyncRequestTest, AsyncExecuteWithCancellation) {
    CancellationToken token;

    auto coro = runVoidTask([&token]() -> Task<void> {
        RequestInfo info;
        info.url = "http://httpbingo.org/delay/5";
        info.methodType = HttpMethodType::Get;
        info.readTimeout = std::chrono::milliseconds(10000);

        HttpRequest request;
        auto response = co_await request.asyncExecute(info, token);

        EXPECT_FALSE(response.isSuccess());
        EXPECT_EQ(response.error.retCode, slark::Error::Cancelled);
    });

    std::thread canceller([&token]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        token.cancel();
    });

    resumeAndWait(coro);
    EXPECT_TRUE(coro.isDone());

    canceller.join();
}

TEST(HttpAsyncRequestTest, AsyncExecuteWithTimeout) {
    auto coro = []() -> Task<void> {
        RequestInfo info;
        info.url = "http://httpbingo.org/delay/10";
        info.methodType = HttpMethodType::Get;
        info.readTimeout = std::chrono::milliseconds(1000);

        HttpRequest request;
        auto response = co_await request.asyncExecute(info);

        EXPECT_FALSE(response.isSuccess());
        EXPECT_EQ(response.error.retCode, slark::Error::ConnectionTimeout);
    }();

    resumeAndWait(coro);
    EXPECT_TRUE(coro.isDone());
}

TEST(HttpAsyncRequestTest, AsyncExecuteInvalidUrl) {
    auto coro = []() -> Task<void> {
        RequestInfo info;
        info.url = "not-a-valid-url";
        info.methodType = HttpMethodType::Get;
        info.readTimeout = std::chrono::milliseconds(1000);

        HttpRequest request;
        auto response = co_await request.asyncExecute(info);

        EXPECT_FALSE(response.isSuccess());
        EXPECT_NE(response.error.retCode, slark::Error::Success);
    }();

    resumeAndWait(coro);
    EXPECT_TRUE(coro.isDone());
}

TEST(HttpAsyncRequestTest, LazyTaskOutlivesHttpRequest) {
    RequestInfo info;
    info.url = "not-a-valid-url";
    info.methodType = HttpMethodType::Get;

    Task<HttpResponse> requestTask;
    {
        HttpRequest request;
        requestTask = request.asyncExecute(info);
    }

    requestTask.resume();
    ASSERT_TRUE(requestTask.isDone());
    const auto response = requestTask.result();
    EXPECT_EQ(response.error.retCode, slark::Error::InvalidArgument);
}

TEST(HttpAsyncRequestTest, LazyGeneratorOutlivesHttpRequest) {
    RequestInfo info;
    info.url = "not-a-valid-url";
    info.methodType = HttpMethodType::Get;

    AsyncGenerator<StreamChunk> generator;
    {
        HttpRequest request;
        generator = request.streamingExecute(info);
    }

    Error observedError = slark::Error::Success;
    auto consumer = runVoidTask([&]() -> Task<void> {
        auto chunk = co_await generator.next();
        EXPECT_TRUE(chunk.has_value());
        if (!chunk.has_value()) {
            co_return;
        }
        EXPECT_TRUE(chunk->hasError());
        if (chunk->hasError()) {
            observedError = chunk->error.retCode;
        }
    });
    consumer.resume();

    ASSERT_TRUE(consumer.isDone());
    EXPECT_EQ(observedError, slark::Error::InvalidArgument);
}

TEST(HttpAsyncRequestTest, AsyncExecuteHttps) {
    auto coro = []() -> Task<void> {
        RequestInfo info;
        info.url = "https://httpbingo.org/get";
        info.methodType = HttpMethodType::Get;
        info.readTimeout = std::chrono::milliseconds(5000);

        HttpRequest request;
        auto response = co_await request.asyncExecute(info);

        EXPECT_TRUE(response.isSuccess());
        EXPECT_EQ(response.statusCode, HttpStatusCode::OK);
    }();

    resumeAndWait(coro);
    EXPECT_TRUE(coro.isDone());
}

TEST(HttpAsyncRequestTest, StreamingExecute) {
    auto coro = []() -> Task<void> {
        RequestInfo info;
        info.url = "http://httpbingo.org/stream-bytes/1024";
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

    resumeAndWait(coro);
    EXPECT_TRUE(coro.isDone());
}

TEST(HttpAsyncRequestTest, StreamingExecuteWithCancellation) {
    StreamingCancellationServer server;
    size_t receivedBytes = 0;
    bool receivedHeaders = false;
    Error terminalError = Error::Success;

    auto coro = runVoidTask([&]() -> Task<void> {
        RequestInfo info;
        info.url = server.url();
        info.methodType = HttpMethodType::Get;
        info.networkAddressPolicy = NetworkAddressPolicy::AllowAny;
        info.ipVersion = IPVersion::V4;
        info.readTimeout = std::chrono::seconds(5);
        info.readIdleTimeout = std::chrono::seconds(5);

        HttpRequest request;
        auto gen = request.streamingExecute(info);
        while (true) {
            auto chunk = co_await gen.next();
            if (!chunk.has_value()) {
                break;
            }
            if (chunk->statusCode == HttpStatusCode::OK) {
                receivedHeaders = true;
            }
            if (chunk->hasData()) {
                receivedBytes += chunk->data->length();
                request.cancel();
                continue;
            }
            if (chunk->hasError()) {
                terminalError = chunk->error.retCode;
                break;
            }
        }
    });

    resumeAndWait(coro);
    ASSERT_TRUE(coro.isDone());
    EXPECT_TRUE(receivedHeaders);
    EXPECT_EQ(receivedBytes, 1024u);
    EXPECT_EQ(terminalError, Error::Cancelled);

    for (int i = 0; i < 100 && !server.clientDisconnected(); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    EXPECT_TRUE(server.clientDisconnected());
}

TEST(HttpAsyncRequestTest, ConcurrentRequests) {
    std::atomic<int32_t> successCount{0};
    std::atomic<int32_t> failCount{0};

    constexpr int32_t kRequestCount = 5;
    std::vector<std::shared_ptr<Task<void>>> coroutines;

    for (int32_t i = 0; i < kRequestCount; ++i) {
        auto coro =
            std::make_shared<Task<void>>(runVoidTask([&successCount, &failCount]() -> Task<void> {
                RequestInfo info;
                info.url = "http://httpbingo.org/get";
                info.methodType = HttpMethodType::Get;
                info.readTimeout = std::chrono::milliseconds(10000);

                HttpRequest request;
                auto response = co_await request.asyncExecute(info);

                if (response.isSuccess()) {
                    successCount++;
                } else {
                    failCount++;
                }
            }));
        coroutines.push_back(coro);
        coro->resume();
    }

    for (int32_t i = 0; i < 200 && successCount.load() + failCount.load() < kRequestCount; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    EXPECT_EQ(successCount.load() + failCount.load(), kRequestCount);
    EXPECT_GT(successCount.load(), 0);
}

TEST(HttpAsyncRequestTest, RedirectFollowing) {
    auto coro = []() -> Task<void> {
        RequestInfo info;
        info.url = "http://httpbingo.org/redirect/2";
        info.methodType = HttpMethodType::Get;
        info.readTimeout = std::chrono::milliseconds(5000);
        info.isAllowRedirect = true;

        HttpRequest request;
        auto response = co_await request.asyncExecute(info);

        EXPECT_TRUE(response.isSuccess());
        EXPECT_EQ(response.statusCode, HttpStatusCode::OK);
    }();

    resumeAndWait(coro);
    EXPECT_TRUE(coro.isDone());
}

TEST(HttpAsyncRequestTest, RedirectDisabled) {
    auto coro = []() -> Task<void> {
        RequestInfo info;
        info.url = "http://httpbingo.org/redirect/1";
        info.methodType = HttpMethodType::Get;
        info.readTimeout = std::chrono::milliseconds(5000);
        info.isAllowRedirect = false;

        HttpRequest request;
        auto response = co_await request.asyncExecute(info);

        EXPECT_TRUE(response.isNeedRedirect());
    }();

    resumeAndWait(coro);
    EXPECT_TRUE(coro.isDone());
}

TEST(HttpAsyncRequestTest, CustomHeaders) {
    auto coro = []() -> Task<void> {
        RequestInfo info;
        info.url = "http://httpbingo.org/headers";
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

    resumeAndWait(coro);
    EXPECT_TRUE(coro.isDone());
}

TEST(HttpAsyncRequestTest, UserAgent) {
    auto coro = []() -> Task<void> {
        RequestInfo info;
        info.url = "http://httpbingo.org/user-agent";
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

    resumeAndWait(coro);
    EXPECT_TRUE(coro.isDone());
}

TEST(HttpAsyncRequestTest, PutMethod) {
    auto coro = []() -> Task<void> {
        RequestInfo info;
        info.url = "http://httpbingo.org/put";
        info.methodType = HttpMethodType::Put;
        info.readTimeout = std::chrono::milliseconds(5000);
        info.headers["Content-Type"] = "application/json";
        info.body = std::make_shared<Data>(R"({"update": "data"})");

        HttpRequest request;
        auto response = co_await request.asyncExecute(info);

        EXPECT_TRUE(response.isSuccess());
        EXPECT_EQ(response.statusCode, HttpStatusCode::OK);
    }();

    resumeAndWait(coro);
    EXPECT_TRUE(coro.isDone());
}

TEST(HttpAsyncRequestTest, PatchMethod) {
    auto coro = []() -> Task<void> {
        RequestInfo info;
        info.url = "http://httpbingo.org/patch";
        info.methodType = HttpMethodType::Patch;
        info.readTimeout = std::chrono::milliseconds(5000);
        info.headers["Content-Type"] = "application/json";
        info.body = std::make_shared<Data>(R"({"patch": "data"})");

        HttpRequest request;
        auto response = co_await request.asyncExecute(info);

        EXPECT_TRUE(response.isSuccess());
        EXPECT_EQ(response.statusCode, HttpStatusCode::OK);
    }();

    resumeAndWait(coro);
    EXPECT_TRUE(coro.isDone());
}

TEST(HttpAsyncRequestTest, DeleteMethod) {
    auto coro = []() -> Task<void> {
        RequestInfo info;
        info.url = "http://httpbingo.org/delete";
        info.methodType = HttpMethodType::Delete;
        info.readTimeout = std::chrono::milliseconds(5000);

        HttpRequest request;
        auto response = co_await request.asyncExecute(info);

        EXPECT_TRUE(response.isSuccess());
        EXPECT_EQ(response.statusCode, HttpStatusCode::OK);
    }();

    resumeAndWait(coro);
    EXPECT_TRUE(coro.isDone());
}

TEST(HttpAsyncRequestTest, NotFoundStatus) {
    auto coro = []() -> Task<void> {
        RequestInfo info;
        info.url = "http://httpbingo.org/status/404";
        info.methodType = HttpMethodType::Get;
        info.readTimeout = std::chrono::milliseconds(5000);

        HttpRequest request;
        auto response = co_await request.asyncExecute(info);

        EXPECT_EQ(response.statusCode, HttpStatusCode::NotFound);
    }();

    resumeAndWait(coro);
    EXPECT_TRUE(coro.isDone());
}

TEST(HttpAsyncRequestTest, ServerErrorStatus) {
    auto coro = []() -> Task<void> {
        RequestInfo info;
        info.url = "http://httpbingo.org/status/500";
        info.methodType = HttpMethodType::Get;
        info.readTimeout = std::chrono::milliseconds(5000);

        HttpRequest request;
        auto response = co_await request.asyncExecute(info);

        EXPECT_EQ(response.statusCode, HttpStatusCode::InternalServerError);
    }();

    resumeAndWait(coro);
    EXPECT_TRUE(coro.isDone());
}

TEST(HttpAsyncRequestIntegrationTest, ScheduledHttpRequest) {
    std::atomic<bool> completed{false};

    auto coro = runVoidTask([&]() -> Task<void> {
        RequestInfo info;
        info.url = "http://httpbingo.org/get";
        info.methodType = HttpMethodType::Get;
        info.readTimeout = std::chrono::milliseconds(5000);

        HttpRequest request;
        auto response = co_await request.asyncExecute(info);

        EXPECT_TRUE(response.isSuccess());
        completed = true;
    });

    resumeAndWait(coro);
    EXPECT_TRUE(coro.isDone());
    EXPECT_TRUE(completed.load());
}
