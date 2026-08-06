//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//
#include "HttpProcessor.h"
#include "HttpUtil.hpp"
#include "Url.h"

#include <gtest/gtest.h>

#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include "ThreadPool.hpp"

using namespace slark;
using namespace slark::http;

namespace {

class LoopbackHttpServer final {
 public:
    explicit LoopbackHttpServer(std::string response) : response_(std::move(response)) {
        listenFd_ = ::socket(AF_INET, SOCK_STREAM, 0);
        if (listenFd_ < 0) {
            throw std::runtime_error("failed to create loopback socket");
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
            throw std::runtime_error("failed to bind loopback socket");
        }

        socklen_t addressLength = sizeof(address);
        if (::getsockname(listenFd_, reinterpret_cast<sockaddr*>(&address), &addressLength) < 0) {
            ::close(listenFd_);
            listenFd_ = -1;
            throw std::runtime_error("failed to get loopback port");
        }
        port_ = ntohs(address.sin_port);
        worker_ = std::thread([this] { serveOneRequest(); });
    }

    ~LoopbackHttpServer() {
        if (worker_.joinable()) {
            worker_.join();
        }
        if (listenFd_ >= 0) {
            ::close(listenFd_);
        }
    }

    LoopbackHttpServer(const LoopbackHttpServer&) = delete;
    LoopbackHttpServer& operator=(const LoopbackHttpServer&) = delete;

    [[nodiscard]] uint16_t port() const noexcept {
        return port_;
    }

 private:
    void serveOneRequest() noexcept {
        const int clientFd = ::accept(listenFd_, nullptr, nullptr);
        if (clientFd < 0) {
            return;
        }

        std::array<char, 4096> buffer{};
        std::string request;
        while (request.find("\r\n\r\n") == std::string::npos) {
            const auto received = ::recv(clientFd, buffer.data(), buffer.size(), 0);
            if (received <= 0) {
                ::close(clientFd);
                return;
            }
            request.append(buffer.data(), static_cast<size_t>(received));
        }

        size_t sent = 0;
        while (sent < response_.size()) {
            const auto written = ::send(clientFd, response_.data() + sent,
                                        response_.size() - sent, 0);
            if (written <= 0) {
                break;
            }
            sent += static_cast<size_t>(written);
        }
        ::shutdown(clientFd, SHUT_WR);
        ::close(clientFd);
        ::close(listenFd_);
        listenFd_ = -1;
    }

    std::string response_;
    int listenFd_ = -1;
    uint16_t port_ = 0;
    std::thread worker_;
};

class LoopbackBlackholeServer final {
 public:
    LoopbackBlackholeServer() {
        listenFd_ = ::socket(AF_INET, SOCK_STREAM, 0);
        if (listenFd_ < 0) {
            throw std::runtime_error("failed to create blackhole socket");
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
            throw std::runtime_error("failed to bind blackhole socket");
        }

        socklen_t addressLength = sizeof(address);
        if (::getsockname(listenFd_, reinterpret_cast<sockaddr*>(&address), &addressLength) < 0) {
            ::close(listenFd_);
            listenFd_ = -1;
            throw std::runtime_error("failed to get blackhole port");
        }
        port_ = ntohs(address.sin_port);
        worker_ = std::thread([this] { holdConnection(); });
    }

    ~LoopbackBlackholeServer() {
        if (listenFd_ >= 0) {
            ::shutdown(listenFd_, SHUT_RDWR);
        }
        {
            std::lock_guard lock(mutex_);
            stop_ = true;
        }
        condition_.notify_one();
        if (worker_.joinable()) {
            worker_.join();
        }
        if (listenFd_ >= 0) {
            ::close(listenFd_);
        }
    }

    LoopbackBlackholeServer(const LoopbackBlackholeServer&) = delete;
    LoopbackBlackholeServer& operator=(const LoopbackBlackholeServer&) = delete;

    [[nodiscard]] uint16_t port() const noexcept {
        return port_;
    }

 private:
    void holdConnection() noexcept {
        const int clientFd = ::accept(listenFd_, nullptr, nullptr);
        if (clientFd < 0) {
            return;
        }

        std::unique_lock lock(mutex_);
        condition_.wait(lock, [this] { return stop_; });
        lock.unlock();
        ::close(clientFd);
    }

    int listenFd_ = -1;
    uint16_t port_ = 0;
    std::mutex mutex_;
    std::condition_variable condition_;
    bool stop_ = false;
    std::thread worker_;
};

HttpResponse executeLoopback(const std::string& response) {
    LoopbackHttpServer server(response);

    RequestInfo info;
    info.url = "http://127.0.0.1:" + std::to_string(server.port()) + "/";
    info.methodType = HttpMethodType::Get;
    info.ipVersion = IPVersion::V4;
    info.connectTimeout = std::chrono::seconds(2);
    info.readTimeout = std::chrono::seconds(2);
    info.readIdleTimeout = std::chrono::seconds(2);

    ThreadPool pool(ThreadPoolConfig{2});
    std::atomic<bool> done{false};
    HttpResponse result;
    auto task = [&]() -> Task<void> {
        HttpProcessor processor;
        auto responseResult = co_await processor.process(info);
        if (responseResult.has_value()) {
            result = std::move(responseResult.value());
        } else {
            result.error = responseResult.error();
        }
        done.store(true);
    }();
    task.resume();

    for (int i = 0; i < 200 && !done.load(); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    EXPECT_TRUE(done.load());
    return result;
}

}  // namespace

TEST(HttpLoopbackTest, UnknownLengthResponseCompletesOnPeerEof) {
    const auto response = executeLoopback("HTTP/1.1 200 OK\r\nConnection: close\r\n\r\nhello");

    EXPECT_EQ(response.error.retCode, Error::Success);
    EXPECT_EQ(response.statusCode, HttpStatusCode::OK);
    ASSERT_NE(response.body, nullptr);
    EXPECT_EQ(std::string(reinterpret_cast<const char*>(response.body->data()),
                          response.body->length()),
              "hello");
}

TEST(HttpLoopbackTest, FixedLengthResponseTruncatedAtPeerEofFails) {
    const auto response = executeLoopback(
        "HTTP/1.1 200 OK\r\nContent-Length: 5\r\nConnection: close\r\n\r\nhel");

    EXPECT_EQ(response.error.retCode, Error::ConnectionReset);
}

TEST(HttpLoopbackTest, ChunkedResponseTruncatedAtPeerEofFails) {
    const auto response = executeLoopback(
        "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\nConnection: close\r\n\r\n"
        "5\r\nhel");

    EXPECT_EQ(response.error.retCode, Error::ConnectionReset);
}

TEST(HttpLoopbackTest, WriteTimeoutPreservesConnectionTimeout) {
    LoopbackBlackholeServer server;
    RequestInfo info;
    info.url = "http://127.0.0.1:" + std::to_string(server.port()) + "/";
    info.methodType = HttpMethodType::Post;
    info.ipVersion = IPVersion::V4;
    info.connectTimeout = std::chrono::seconds(2);
    info.writeTimeout = std::chrono::milliseconds(100);
    info.readTimeout = std::chrono::seconds(2);
    info.readIdleTimeout = std::chrono::seconds(2);
    info.body = std::make_unique<Data>(std::string(8 * 1024 * 1024, 'x'));

    ThreadPool pool(ThreadPoolConfig{2});
    std::atomic<bool> done{false};
    HttpResponse result;
    auto task = [&]() -> Task<void> {
        HttpProcessor processor;
        auto response = co_await processor.process(info);
        if (response.has_value()) {
            result = std::move(response.value());
        } else {
            result.error = response.error();
        }
        done.store(true);
    }();
    task.resume();

    for (int i = 0; i < 200 && !done.load(); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    ASSERT_TRUE(done.load());
    EXPECT_EQ(result.error.retCode, Error::ConnectionTimeout);
}

TEST(HttpUtilLoopbackTest, IPv6HostUsesAuthorityBrackets) {
    RequestInfo info;
    info.url = "http://[2001:db8::1]:8080/path";
    info.methodType = HttpMethodType::Get;
    const Url url(info.url);

    const auto request = Util::buildRequestString(info, url, false);

    EXPECT_NE(request.find("Host: [2001:db8::1]:8080\r\n"), std::string::npos);
}

TEST(HttpUtilLoopbackTest, RejectsControlCharactersInUrlComponents) {
    for (const auto urlText : {"http://example.com/path\r", "http://example.com/path\n",
                               "http://example.com/path\t", "http://example.com/path "}) {
        RequestInfo info;
        info.url = urlText;
        info.methodType = HttpMethodType::Get;
        const Url url(info.url);

        EXPECT_FALSE(url.isValid()) << urlText;
        EXPECT_FALSE(Util::validateRequest(info, url)) << urlText;
        EXPECT_TRUE(Util::buildRequestString(info, url, false).empty()) << urlText;
    }

    std::string nulUrl = "http://example.com/path";
    nulUrl.push_back('\0');
    nulUrl.append("tail");
    RequestInfo nulInfo;
    nulInfo.url = nulUrl;
    nulInfo.methodType = HttpMethodType::Get;
    const Url parsedNulUrl(nulInfo.url);
    EXPECT_FALSE(parsedNulUrl.isValid());
    EXPECT_FALSE(Util::validateRequest(nulInfo, parsedNulUrl));
    EXPECT_TRUE(Util::buildRequestString(nulInfo, parsedNulUrl, false).empty());

    std::string delUrl = "http://example.com/path";
    delUrl.push_back('\x7f');
    RequestInfo delInfo;
    delInfo.url = delUrl;
    delInfo.methodType = HttpMethodType::Get;
    const Url parsedDelUrl(delInfo.url);
    EXPECT_FALSE(parsedDelUrl.isValid());
    EXPECT_FALSE(Util::validateRequest(delInfo, parsedDelUrl));
    EXPECT_TRUE(Util::buildRequestString(delInfo, parsedDelUrl, false).empty());
}
