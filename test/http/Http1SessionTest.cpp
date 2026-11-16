//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#include "Http1Session.h"
#include "HttpProcessor.h"
#include "HttpResponseParser.h"
#include "ThreadPool.hpp"

#include <gtest/gtest.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

using namespace slark;
using namespace slark::http;

namespace {

constexpr int kStopPollMs = 20;

class KeepAliveServer final {
 public:
    explicit KeepAliveServer(std::vector<std::string> responses, bool combineResponses = false)
        : responses_(std::move(responses)), combineResponses_(combineResponses) {
        listenFd_ = ::socket(AF_INET, SOCK_STREAM, 0);
        if (listenFd_ < 0) {
            throw std::runtime_error("failed to create loopback socket");
        }

        int reuseAddress = 1;
        if (::setsockopt(listenFd_, SOL_SOCKET, SO_REUSEADDR, &reuseAddress, sizeof(reuseAddress)) < 0) {
            ::close(listenFd_);
            listenFd_ = -1;
            throw std::runtime_error("failed to set socket options");
        }

        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        address.sin_port = htons(0);
        if (::bind(listenFd_, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) < 0 ||
            ::listen(listenFd_, 4) < 0) {
            ::close(listenFd_);
            listenFd_ = -1;
            throw std::runtime_error("failed to bind loopback socket");
        }

        socklen_t addressLength = sizeof(address);
        if (::getsockname(listenFd_, reinterpret_cast<sockaddr*>(&address), &addressLength) < 0) {
            ::close(listenFd_);
            listenFd_ = -1;
            throw std::runtime_error("failed to get loopback port");
        }
        port_ = ntohs(address.sin_port);
        worker_ = std::thread([this] { serve(); });
    }

    ~KeepAliveServer() {
        // listenFd_ is only touched after the worker has been joined.
        stopped_ = true;
        if (worker_.joinable()) {
            worker_.join();
        }
        if (listenFd_ >= 0) {
            ::close(listenFd_);
            listenFd_ = -1;
        }
    }

    KeepAliveServer(const KeepAliveServer&) = delete;
    KeepAliveServer& operator=(const KeepAliveServer&) = delete;

    [[nodiscard]] uint16_t port() const noexcept {
        return port_;
    }

    [[nodiscard]] int acceptCount() const noexcept {
        return acceptCount_.load();
    }

    [[nodiscard]] int requestCount() const noexcept {
        return requestCount_.load();
    }

 private:
    static bool receiveRequest(int fd) {
        std::string request;
        std::array<char, 4096> buffer{};
        while (request.find("\r\n\r\n") == std::string::npos) {
            const auto received = ::recv(fd, buffer.data(), buffer.size(), 0);
            if (received <= 0) {
                return false;
            }
            request.append(buffer.data(), static_cast<size_t>(received));
        }
        return true;
    }

    static bool sendAll(int fd, const std::string& response) {
        size_t sent = 0;
        while (sent < response.size()) {
            const auto written = ::send(fd, response.data() + sent, response.size() - sent, 0);
            if (written <= 0) {
                return false;
            }
            sent += static_cast<size_t>(written);
        }
        return true;
    }

    // shutdown() does not wake accept() on every platform, so the worker polls and checks stopped_.
    bool waitForClient() const {
        pollfd entry{listenFd_, POLLIN, 0};
        return ::poll(&entry, 1, kStopPollMs) > 0;
    }

    void serve() noexcept {
        size_t responseIndex = 0;
        while (responseIndex < responses_.size() && !stopped_) {
            if (!waitForClient()) {
                continue;
            }
            const int clientFd = ::accept(listenFd_, nullptr, nullptr);
            if (clientFd < 0) {
                continue;
            }
            acceptCount_.fetch_add(1);

            while (responseIndex < responses_.size()) {
                const auto& response = responses_[responseIndex];
                if (!receiveRequest(clientFd)) {
                    ::close(clientFd);
                    return;
                }
                requestCount_.fetch_add(1);
                if (combineResponses_ && responseIndex == 0 && responses_.size() > 1) {
                    if (!sendAll(clientFd, response + responses_[1])) {
                        ::close(clientFd);
                        return;
                    }
                    responseIndex += 2;
                    if (!receiveRequest(clientFd)) {
                        ::close(clientFd);
                        return;
                    }
                    requestCount_.fetch_add(1);
                    break;
                }
                if (!sendAll(clientFd, response)) {
                    ::close(clientFd);
                    return;
                }
                ++responseIndex;
                if (response.find("Connection: close") != std::string::npos) {
                    break;
                }
            }
            ::shutdown(clientFd, SHUT_WR);
            ::close(clientFd);
        }
    }

    int listenFd_ = -1;
    uint16_t port_ = 0;
    std::vector<std::string> responses_;
    bool combineResponses_ = false;
    std::atomic<int> acceptCount_{0};
    std::atomic<int> requestCount_{0};
    std::atomic<bool> stopped_{false};
    std::thread worker_;
};

class BlockingHttpServer final {
 public:
    BlockingHttpServer() {
        listenFd_ = ::socket(AF_INET, SOCK_STREAM, 0);
        if (listenFd_ < 0) {
            throw std::runtime_error("failed to create blocking server socket");
        }

        int reuseAddress = 1;
        ::setsockopt(listenFd_, SOL_SOCKET, SO_REUSEADDR, &reuseAddress, sizeof(reuseAddress));

        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        address.sin_port = htons(0);
        if (::bind(listenFd_, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) < 0 ||
            ::listen(listenFd_, 1) < 0) {
            ::close(listenFd_);
            listenFd_ = -1;
            throw std::runtime_error("failed to bind blocking server socket");
        }

        socklen_t addressLength = sizeof(address);
        if (::getsockname(listenFd_, reinterpret_cast<sockaddr*>(&address), &addressLength) < 0) {
            ::close(listenFd_);
            listenFd_ = -1;
            throw std::runtime_error("failed to get blocking server port");
        }
        port_ = ntohs(address.sin_port);
        worker_ = std::thread([this] { serve(); });
    }

    ~BlockingHttpServer() {
        {
            std::lock_guard lock(mutex_);
            stop_ = true;
        }
        condition_.notify_all();
        if (worker_.joinable()) {
            worker_.join();
        }
        if (listenFd_ >= 0) {
            ::close(listenFd_);
            listenFd_ = -1;
        }
    }

    BlockingHttpServer(const BlockingHttpServer&) = delete;
    BlockingHttpServer& operator=(const BlockingHttpServer&) = delete;

    [[nodiscard]] uint16_t port() const noexcept {
        return port_;
    }

    bool waitForRequest(std::chrono::milliseconds timeout) {
        std::unique_lock lock(mutex_);
        return condition_.wait_for(lock, timeout, [this] { return requestReceived_; });
    }

 private:
    static bool receiveRequest(int fd) {
        std::array<char, 4096> buffer{};
        std::string request;
        while (request.find("\r\n\r\n") == std::string::npos) {
            const auto received = ::recv(fd, buffer.data(), buffer.size(), 0);
            if (received <= 0) {
                return false;
            }
            request.append(buffer.data(), static_cast<size_t>(received));
        }
        return true;
    }

    bool waitForClient() const {
        while (!stop_) {
            pollfd entry{listenFd_, POLLIN, 0};
            if (::poll(&entry, 1, kStopPollMs) > 0) {
                return true;
            }
        }
        return false;
    }

    void serve() noexcept {
        if (!waitForClient()) {
            return;
        }
        const int clientFd = ::accept(listenFd_, nullptr, nullptr);
        if (clientFd < 0) {
            return;
        }

        if (!receiveRequest(clientFd)) {
            ::close(clientFd);
            return;
        }
        {
            std::lock_guard lock(mutex_);
            requestReceived_ = true;
        }
        condition_.notify_all();

        std::unique_lock lock(mutex_);
        condition_.wait(lock, [this] { return stop_.load(); });
        lock.unlock();
        ::close(clientFd);
    }

    int listenFd_ = -1;
    uint16_t port_ = 0;
    std::mutex mutex_;
    std::condition_variable condition_;
    bool requestReceived_ = false;
    std::atomic<bool> stop_{false};
    std::thread worker_;
};

RequestInfo makeRequest(uint16_t port, std::shared_ptr<Http1Session> session) {
    RequestInfo info;
    info.url = "http://127.0.0.1:" + std::to_string(port) + "/";
    info.methodType = HttpMethodType::Get;
    info.connectionMode = HttpConnectionMode::KeepAlive;
    info.session = std::move(session);
    info.ipVersion = IPVersion::V4;
    info.networkAddressPolicy = NetworkAddressPolicy::AllowAny;
    info.connectTimeout = std::chrono::seconds(2);
    info.readTimeout = std::chrono::seconds(2);
    info.readIdleTimeout = std::chrono::seconds(2);
    return info;
}

struct PairResult final {
    HttpResponse first;
    HttpResponse second;
    std::atomic<bool> done{false};
};

void waitFor(const std::atomic<bool>& done) {
    for (int i = 0; i < 200 && !done.load(); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}

template <typename Func>
Task<void> runVoidTask(Func func) {
    co_await func();
}

std::string fixedResponse(std::string body, bool closeConnection = false) {
    return "HTTP/1.1 200 OK\r\nContent-Length: " + std::to_string(body.size()) +
           "\r\nConnection: " + (closeConnection ? "close" : "keep-alive") + "\r\n\r\n" + body;
}

}  // namespace

TEST(Http1SessionTest, SequentialRequestsReuseOneConnection) {
    KeepAliveServer server({fixedResponse("one"), fixedResponse("two")}, true);
    auto session = std::make_shared<Http1Session>();
    PairResult result;

    ThreadPool pool(ThreadPoolConfig{2});
    auto task = runVoidTask([&]() -> Task<void> {
        auto first = co_await HttpProcessor::process(makeRequest(server.port(), session));
        if (first) {
            result.first = std::move(*first);
        } else {
            result.first.error = first.error();
        }
        auto second = co_await HttpProcessor::process(makeRequest(server.port(), session));
        if (second) {
            result.second = std::move(*second);
        } else {
            result.second.error = second.error();
        }
        result.done.store(true);
    });
    task.resume();
    waitFor(result.done);

    ASSERT_TRUE(result.done.load());
    EXPECT_EQ(result.first.error.retCode, Error::Success);
    EXPECT_EQ(result.second.error.retCode, Error::Success);
    ASSERT_NE(result.first.body, nullptr);
    ASSERT_NE(result.second.body, nullptr);
    EXPECT_EQ(result.first.body->toString(), "one");
    EXPECT_EQ(result.second.body->toString(), "two");
    EXPECT_EQ(server.acceptCount(), 1);
    EXPECT_EQ(server.requestCount(), 2);
}

TEST(Http1SessionTest, ConnectionCloseResponseForcesReconnect) {
    KeepAliveServer server({fixedResponse("one", true), fixedResponse("two")});
    auto session = std::make_shared<Http1Session>();
    PairResult result;

    ThreadPool pool(ThreadPoolConfig{2});
    auto task = runVoidTask([&]() -> Task<void> {
        auto first = co_await HttpProcessor::process(makeRequest(server.port(), session));
        if (first) {
            result.first = std::move(*first);
        } else {
            result.first.error = first.error();
        }
        auto second = co_await HttpProcessor::process(makeRequest(server.port(), session));
        if (second) {
            result.second = std::move(*second);
        } else {
            result.second.error = second.error();
        }
        result.done.store(true);
    });
    task.resume();
    waitFor(result.done);

    ASSERT_TRUE(result.done.load());
    EXPECT_EQ(result.first.error.retCode, Error::Success);
    EXPECT_EQ(result.second.error.retCode, Error::Success);
    EXPECT_EQ(server.acceptCount(), 2);
    EXPECT_EQ(server.requestCount(), 2);
}

TEST(Http1SessionTest, HttpErrorPreservesBodyAndTrailersBeforeReuse) {
    const std::string errorResponse =
        "HTTP/1.1 500 Internal Server Error\r\nTransfer-Encoding: chunked\r\n\r\n"
        "5\r\nerror\r\n0\r\nX-Trace: keepalive-error\r\n\r\n";
    KeepAliveServer server({errorResponse, fixedResponse("reused")});
    auto session = std::make_shared<Http1Session>();
    PairResult result;

    ThreadPool pool(ThreadPoolConfig{2});
    auto task = runVoidTask([&]() -> Task<void> {
        auto first = co_await HttpProcessor::process(makeRequest(server.port(), session));
        if (first) {
            result.first = std::move(*first);
        } else {
            result.first.error = first.error();
        }
        auto second = co_await HttpProcessor::process(makeRequest(server.port(), session));
        if (second) {
            result.second = std::move(*second);
        } else {
            result.second.error = second.error();
        }
        result.done.store(true);
    });
    task.resume();
    waitFor(result.done);

    ASSERT_TRUE(result.done.load());
    EXPECT_EQ(result.first.statusCode, HttpStatusCode::InternalServerError);
    EXPECT_EQ(result.first.error.retCode, Error::HttpError);
    ASSERT_NE(result.first.body, nullptr);
    EXPECT_EQ(result.first.body->toString(), "error");
    EXPECT_EQ(result.first.trailers.at("x-trace"), "keepalive-error");
    EXPECT_EQ(result.second.error.retCode, Error::Success);
    ASSERT_NE(result.second.body, nullptr);
    EXPECT_EQ(result.second.body->toString(), "reused");
    EXPECT_EQ(server.acceptCount(), 1);
    EXPECT_EQ(server.requestCount(), 2);
}

TEST(HttpResponseParserTest, TakesUnreadDataForNextResponse) {
    HttpResponseParser parser({});
    const std::string first = "HTTP/1.1 200 OK\r\nContent-Length: 3\r\n\r\none";
    const std::string second = "HTTP/1.1 200 OK\r\nContent-Length: 3\r\n\r\ntwo";
    const auto combined = first + second;

    parser.feed(reinterpret_cast<const Byte*>(combined.data()), combined.size());

    ASSERT_TRUE(parser.isCompleted());
    EXPECT_TRUE(parser.canReuseConnection());
    EXPECT_EQ(parser.takeUnreadData(), second);
    parser.resetForNextResponse();
    EXPECT_EQ(parser.state(), HttpResponseParser::State::StatusLine);
}

TEST(HttpResponseParserTest, Http10ResponseCannotBeReused) {
    HttpResponseParser parser({});
    constexpr std::string_view response =
        "HTTP/1.0 200 OK\r\nContent-Length: 3\r\n\r\none";

    parser.feed(reinterpret_cast<const Byte*>(response.data()), response.size());

    ASSERT_TRUE(parser.isCompleted());
    EXPECT_TRUE(parser.isHttp10());
    EXPECT_TRUE(parser.shouldCloseConnection());
    EXPECT_FALSE(parser.canReuseConnection());
}

TEST(HttpResponseParserTest, EofDelimitedResponseCannotBeReused) {
    HttpResponseParser parser({});
    constexpr std::string_view response = "HTTP/1.1 200 OK\r\n\r\none";

    parser.feed(reinterpret_cast<const Byte*>(response.data()), response.size());
    ASSERT_FALSE(parser.isCompleted());
    parser.onEof();

    EXPECT_TRUE(parser.isCompleted());
    EXPECT_TRUE(parser.endedByEof());
    EXPECT_TRUE(parser.isBodyUnknownLength());
    EXPECT_FALSE(parser.canReuseConnection());
}

TEST(HttpResponseParserTest, ChunkedTrailersAndLeftoverAreReusable) {
    std::unordered_map<std::string, std::string> trailers;
    HttpResponseParser::Callbacks callbacks;
    callbacks.onTrailers = [&trailers](std::unordered_map<std::string, std::string>&& values) {
        trailers = std::move(values);
    };
    HttpResponseParser parser(std::move(callbacks));
    const std::string first =
        "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n"
        "4\r\nWiki\r\n5\r\npedia\r\n0\r\nX-Trace: done\r\n\r\n";
    const std::string second = "HTTP/1.1 204 No Content\r\n\r\n";

    const auto combined = first + second;
    parser.feed(reinterpret_cast<const Byte*>(combined.data()), combined.size());

    ASSERT_TRUE(parser.isCompleted());
    EXPECT_TRUE(parser.canReuseConnection());
    ASSERT_EQ(trailers.size(), 1u);
    EXPECT_EQ(trailers.at("x-trace"), "done");
    EXPECT_EQ(parser.takeUnreadData(), second);
}

TEST(HttpResponseParserTest, TruncatedResponseIsNotReusable) {
    HttpResponseParser parser({});
    constexpr std::string_view response =
        "HTTP/1.1 200 OK\r\nContent-Length: 5\r\n\r\none";

    parser.feed(reinterpret_cast<const Byte*>(response.data()), response.size());
    ASSERT_FALSE(parser.isCompleted());
    parser.onEof();

    EXPECT_TRUE(parser.hasError());
    EXPECT_FALSE(parser.canReuseConnection());
}

TEST(Http1SessionTest, ParseErrorDiscardsConnectionBeforeNextRequest) {
    const std::string invalidResponse =
        "HTTP/1.1 invalid status\r\nConnection: close\r\n\r\n";
    KeepAliveServer server({invalidResponse, fixedResponse("reconnected")});
    auto session = std::make_shared<Http1Session>();
    std::atomic<bool> done{false};
    Error firstError = Error::Success;
    Error secondError = Error::Success;

    ThreadPool pool(ThreadPoolConfig{2});
    auto task = runVoidTask([&]() -> Task<void> {
        auto first = co_await HttpProcessor::process(makeRequest(server.port(), session));
        firstError = first ? Error::Success : first.error().retCode;
        auto second = co_await HttpProcessor::process(makeRequest(server.port(), session));
        secondError = second ? Error::Success : second.error().retCode;
        done.store(true, std::memory_order_release);
    });
    task.resume();
    waitFor(done);

    ASSERT_TRUE(done.load(std::memory_order_acquire));
    EXPECT_EQ(firstError, Error::HttpResponseInvalid);
    EXPECT_EQ(secondError, Error::Success);
    EXPECT_EQ(server.acceptCount(), 2);
    EXPECT_EQ(server.requestCount(), 2);
}

TEST(Http1SessionTest, CancelledRequestDoesNotOpenConnection) {
    KeepAliveServer server({fixedResponse("unused")});
    auto session = std::make_shared<Http1Session>();
    CancellationToken token;
    token.cancel();
    std::atomic<bool> done{false};
    Error error = Error::Success;

    ThreadPool pool(ThreadPoolConfig{2});
    auto task = runVoidTask([&]() -> Task<void> {
        auto result = co_await HttpProcessor::process(makeRequest(server.port(), session), token);
        error = result ? Error::Success : result.error().retCode;
        done.store(true, std::memory_order_release);
    });
    task.resume();
    waitFor(done);

    ASSERT_TRUE(done.load(std::memory_order_acquire));
    EXPECT_EQ(error, Error::Cancelled);
    EXPECT_EQ(server.acceptCount(), 0);
}

TEST(Http1SessionTest, CloseCancelsActiveReadWithoutWaitingForReadTimeout) {
    BlockingHttpServer server;
    auto session = std::make_shared<Http1Session>();
    std::atomic<bool> connected{false};
    std::atomic<bool> done{false};
    Error error = Error::Success;

    ThreadPool pool(ThreadPoolConfig{2});
    auto task = runVoidTask([&]() -> Task<void> {
        CancellationToken token;
        auto events = session->executeEvents(makeRequest(server.port(), session), token);
        auto connectedEvent = co_await events.next();
        if (!connectedEvent || connectedEvent->type != HttpResponseEventType::Connected) {
            error = Error::ConnectionFailed;
            done.store(true, std::memory_order_release);
            co_return;
        }
        connected.store(true, std::memory_order_release);

        auto terminalEvent = co_await events.next();
        if (terminalEvent && terminalEvent->type == HttpResponseEventType::Error) {
            error = terminalEvent->error.retCode;
        } else {
            error = Error::NetworkError;
        }
        done.store(true, std::memory_order_release);
    });
    task.resume();

    ASSERT_TRUE(server.waitForRequest(std::chrono::seconds(2)));
    for (int i = 0; i < 200 && !connected.load(std::memory_order_acquire); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    ASSERT_TRUE(connected.load(std::memory_order_acquire));

    const auto start = std::chrono::steady_clock::now();
    session->close();
    for (int i = 0; i < 100 && !done.load(std::memory_order_acquire); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                             std::chrono::steady_clock::now() - start)
                             .count();

    ASSERT_TRUE(done.load(std::memory_order_acquire));
    EXPECT_EQ(error, Error::Cancelled);
    EXPECT_LT(elapsed, 1000);
}

TEST(Http1SessionTest, ConcurrentRequestIsRejectedWithoutSharingConnection) {
    KeepAliveServer server({fixedResponse("first")});
    auto session = std::make_shared<Http1Session>();
    CancellationToken firstToken;
    CancellationToken secondToken;
    auto firstEvents = session->executeEvents(makeRequest(server.port(), session), firstToken);
    auto secondEvents = session->executeEvents(makeRequest(server.port(), session), secondToken);
    std::atomic<bool> done{false};
    Error secondError = Error::Success;
    bool firstConnected = false;

    ThreadPool pool(ThreadPoolConfig{2});
    auto task = runVoidTask([&]() -> Task<void> {
        auto firstEvent = co_await firstEvents.next();
        firstConnected = firstEvent.has_value() &&
                         firstEvent->type == HttpResponseEventType::Connected;
        auto secondEvent = co_await secondEvents.next();
        if (secondEvent && secondEvent->type == HttpResponseEventType::Error) {
            secondError = secondEvent->error.retCode;
        }
        while (co_await firstEvents.next()) {
        }
        done.store(true, std::memory_order_release);
    });
    task.resume();
    waitFor(done);

    ASSERT_TRUE(done.load(std::memory_order_acquire));
    EXPECT_TRUE(firstConnected);
    EXPECT_EQ(secondError, Error::InvalidOperation);
    EXPECT_EQ(server.acceptCount(), 1);
}

namespace {

// Serves every request with a tiny keep-alive response and keeps accepting new connections,
// so a test can observe whether the client reused or replaced its connection.
class ScriptedServer final {
public:
    struct Options {
        bool closeAfterResponse = false;
        std::string extraHeaders;
    };

    explicit ScriptedServer(Options options) : options_(std::move(options)) {
        listenFd_ = ::socket(AF_INET, SOCK_STREAM, 0);
        if (listenFd_ < 0) {
            throw std::runtime_error("failed to create scripted server socket");
        }
        int reuseAddress = 1;
        ::setsockopt(listenFd_, SOL_SOCKET, SO_REUSEADDR, &reuseAddress, sizeof(reuseAddress));
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        address.sin_port = htons(0);
        socklen_t length = sizeof(address);
        if (::bind(listenFd_, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) < 0 ||
            ::listen(listenFd_, 4) < 0 ||
            ::getsockname(listenFd_, reinterpret_cast<sockaddr*>(&address), &length) < 0) {
            ::close(listenFd_);
            throw std::runtime_error("failed to set up scripted server socket");
        }
        port_ = ntohs(address.sin_port);
        worker_ = std::thread([this] { serve(); });
    }

    ~ScriptedServer() {
        stopped_ = true;
        if (worker_.joinable()) {
            worker_.join();
        }
        ::close(listenFd_);
    }

    ScriptedServer(const ScriptedServer&) = delete;
    ScriptedServer& operator=(const ScriptedServer&) = delete;

    [[nodiscard]] uint16_t port() const noexcept {
        return port_;
    }

    [[nodiscard]] int acceptCount() const noexcept {
        return acceptCount_.load();
    }

    [[nodiscard]] int requestCount() const noexcept {
        return requestCount_.load();
    }

private:
    bool waitReadable(int fd) const {
        while (!stopped_) {
            pollfd entry{fd, POLLIN, 0};
            const int ready = ::poll(&entry, 1, 50);
            if (ready > 0) {
                return true;
            }
            if (ready < 0 && errno != EINTR) {
                return false;
            }
        }
        return false;
    }

    bool receiveRequest(int fd) const {
        std::string request;
        std::array<char, 4096> buffer{};
        while (request.find("\r\n\r\n") == std::string::npos) {
            if (!waitReadable(fd)) {
                return false;
            }
            const auto received = ::recv(fd, buffer.data(), buffer.size(), 0);
            if (received <= 0) {
                return false;
            }
            request.append(buffer.data(), static_cast<size_t>(received));
        }
        return true;
    }

    void serve() {
        while (waitReadable(listenFd_)) {
            const int clientFd = ::accept(listenFd_, nullptr, nullptr);
            if (clientFd < 0) {
                continue;
            }
            acceptCount_.fetch_add(1);
            const std::string response = "HTTP/1.1 200 OK\r\nContent-Length: 2\r\n"
                                         "Connection: keep-alive\r\n" +
                                         options_.extraHeaders + "\r\nok";
            while (receiveRequest(clientFd)) {
                requestCount_.fetch_add(1);
                if (::send(clientFd, response.data(), response.size(), 0) < 0 ||
                    options_.closeAfterResponse) {
                    break;
                }
            }
            ::close(clientFd);
        }
    }

    Options options_;
    int listenFd_ = -1;
    uint16_t port_ = 0;
    std::atomic<bool> stopped_{false};
    std::atomic<int> acceptCount_{0};
    std::atomic<int> requestCount_{0};
    std::thread worker_;
};

struct FetchState final {
    HttpResponse response;
    std::atomic<bool> done{false};
};

// Runs one request to completion; nullptr when it did not finish in time
std::shared_ptr<FetchState> fetch(uint16_t port, const std::shared_ptr<Http1Session>& session) {
    auto state = std::make_shared<FetchState>();
    auto task = runVoidTask([state, port, session]() -> Task<void> {
        auto result = co_await HttpProcessor::process(makeRequest(port, session));
        if (result) {
            state->response = std::move(*result);
        } else {
            state->response.error = result.error();
        }
        state->done.store(true);
    });
    task.resume();
    waitFor(state->done);
    return state->done.load() ? state : nullptr;
}

}  // namespace

TEST(Http1SessionTest, ReconnectsWhenServerClosedTheIdleConnection) {
    ThreadPool pool(ThreadPoolConfig{2});
    ScriptedServer server({.closeAfterResponse = true});
    auto session = std::make_shared<Http1Session>();

    auto first = fetch(server.port(), session);
    ASSERT_NE(first, nullptr);
    EXPECT_EQ(first->response.error.retCode, Error::Success);

    // Let the server's FIN reach the cached connection
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    auto second = fetch(server.port(), session);
    ASSERT_NE(second, nullptr);
    EXPECT_EQ(second->response.error.retCode, Error::Success);
    ASSERT_NE(second->response.body, nullptr);
    EXPECT_EQ(second->response.body->toString(), "ok");
    EXPECT_EQ(server.acceptCount(), 2);
    EXPECT_EQ(server.requestCount(), 2);
}

TEST(Http1SessionTest, KeepAliveTimeoutHintRetiresTheConnectionEarly) {
    ThreadPool pool(ThreadPoolConfig{2});
    ScriptedServer server({.extraHeaders = "Keep-Alive: timeout=2\r\n"});
    auto session = std::make_shared<Http1Session>();

    auto first = fetch(server.port(), session);
    ASSERT_NE(first, nullptr);
    EXPECT_EQ(first->response.error.retCode, Error::Success);

    // timeout=2 minus the safety margin leaves 1s of idle life
    std::this_thread::sleep_for(std::chrono::milliseconds(1200));

    auto second = fetch(server.port(), session);
    ASSERT_NE(second, nullptr);
    EXPECT_EQ(second->response.error.retCode, Error::Success);
    EXPECT_EQ(server.acceptCount(), 2);
}

TEST(Http1SessionTest, ConnectionWithinKeepAliveTimeoutIsReused) {
    ThreadPool pool(ThreadPoolConfig{2});
    ScriptedServer server({.extraHeaders = "Keep-Alive: timeout=30\r\n"});
    auto session = std::make_shared<Http1Session>();

    auto first = fetch(server.port(), session);
    ASSERT_NE(first, nullptr);
    auto second = fetch(server.port(), session);
    ASSERT_NE(second, nullptr);
    EXPECT_EQ(first->response.error.retCode, Error::Success);
    EXPECT_EQ(second->response.error.retCode, Error::Success);
    EXPECT_EQ(server.acceptCount(), 1);
    EXPECT_EQ(server.requestCount(), 2);
}

TEST(IdleConnectionTest, QuietConnectionIsUsable) {
    int fds[2];
    ASSERT_EQ(::socketpair(AF_UNIX, SOCK_STREAM, 0, fds), 0);
    EXPECT_TRUE(isIdleConnectionUsable(fds[0]));
    ::close(fds[0]);
    ::close(fds[1]);
}

TEST(IdleConnectionTest, PeerCloseMakesConnectionUnusable) {
    int fds[2];
    ASSERT_EQ(::socketpair(AF_UNIX, SOCK_STREAM, 0, fds), 0);
    ::close(fds[1]);
    EXPECT_FALSE(isIdleConnectionUsable(fds[0]));
    ::close(fds[0]);
}

TEST(IdleConnectionTest, UnsolicitedBytesMakeConnectionUnusable) {
    int fds[2];
    ASSERT_EQ(::socketpair(AF_UNIX, SOCK_STREAM, 0, fds), 0);
    ASSERT_EQ(::send(fds[1], "x", 1, 0), 1);
    EXPECT_FALSE(isIdleConnectionUsable(fds[0]));
    // Probing must not consume the byte
    char byte = 0;
    EXPECT_EQ(::recv(fds[0], &byte, 1, MSG_DONTWAIT), 1);
    ::close(fds[0]);
    ::close(fds[1]);
}

TEST(IdleConnectionTest, InvalidSocketIsUnusable) {
    EXPECT_FALSE(isIdleConnectionUsable(kInvalidSocket));
}

namespace {

std::optional<std::chrono::seconds> keepAliveTimeoutOf(std::string_view keepAliveValue) {
    HttpResponseParser parser({});
    std::string response = "HTTP/1.1 200 OK\r\nContent-Length: 0\r\n";
    if (!keepAliveValue.empty()) {
        response += "Keep-Alive: " + std::string(keepAliveValue) + "\r\n";
    }
    response += "\r\n";
    parser.feed(reinterpret_cast<const Byte*>(response.data()), response.size());
    EXPECT_TRUE(parser.isCompleted());
    return parser.keepAliveTimeout();
}

}  // namespace

TEST(HttpResponseParserTest, ParsesKeepAliveTimeout) {
    EXPECT_EQ(keepAliveTimeoutOf("timeout=5, max=100"), std::chrono::seconds(5));
    EXPECT_EQ(keepAliveTimeoutOf("max=100, TIMEOUT=7"), std::chrono::seconds(7));
    EXPECT_EQ(keepAliveTimeoutOf("timeout=0"), std::chrono::seconds(0));
}

TEST(HttpResponseParserTest, IgnoresMissingOrMalformedKeepAliveTimeout) {
    EXPECT_EQ(keepAliveTimeoutOf(""), std::nullopt);
    EXPECT_EQ(keepAliveTimeoutOf("max=100"), std::nullopt);
    EXPECT_EQ(keepAliveTimeoutOf("timeout=abc"), std::nullopt);
    EXPECT_EQ(keepAliveTimeoutOf("timeout=-3"), std::nullopt);
    EXPECT_EQ(keepAliveTimeoutOf("timeout"), std::nullopt);
}
