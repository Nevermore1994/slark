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
#include <sys/socket.h>
#include <unistd.h>

#include <array>
#include <atomic>
#include <chrono>
#include <memory>
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
        if (listenFd_ >= 0) {
            ::shutdown(listenFd_, SHUT_RDWR);
            ::close(listenFd_);
            listenFd_ = -1;
        }
        if (worker_.joinable()) {
            worker_.join();
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

    void serve() noexcept {
        size_t responseIndex = 0;
        while (responseIndex < responses_.size()) {
            const int clientFd = ::accept(listenFd_, nullptr, nullptr);
            if (clientFd < 0) {
                return;
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
    auto task = [&]() -> Task<void> {
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
    }();
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
    auto task = [&]() -> Task<void> {
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
    }();
    task.resume();
    waitFor(result.done);

    ASSERT_TRUE(result.done.load());
    EXPECT_EQ(result.first.error.retCode, Error::Success);
    EXPECT_EQ(result.second.error.retCode, Error::Success);
    EXPECT_EQ(server.acceptCount(), 2);
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
    auto task = [&]() -> Task<void> {
        auto first = co_await HttpProcessor::process(makeRequest(server.port(), session));
        firstError = first ? Error::Success : first.error().retCode;
        auto second = co_await HttpProcessor::process(makeRequest(server.port(), session));
        secondError = second ? Error::Success : second.error().retCode;
        done.store(true, std::memory_order_release);
    }();
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
    auto task = [&]() -> Task<void> {
        auto result = co_await HttpProcessor::process(makeRequest(server.port(), session), token);
        error = result ? Error::Success : result.error().retCode;
        done.store(true, std::memory_order_release);
    }();
    task.resume();
    waitFor(done);

    ASSERT_TRUE(done.load(std::memory_order_acquire));
    EXPECT_EQ(error, Error::Cancelled);
    EXPECT_EQ(server.acceptCount(), 0);
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
    auto task = [&]() -> Task<void> {
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
    }();
    task.resume();
    waitFor(done);

    ASSERT_TRUE(done.load(std::memory_order_acquire));
    EXPECT_TRUE(firstConnected);
    EXPECT_EQ(secondError, Error::InvalidOperation);
    EXPECT_EQ(server.acceptCount(), 1);
}
