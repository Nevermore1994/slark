//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//
#include "HttpProcessor.h"

#include <gtest/gtest.h>

#include <array>
#include <atomic>
#include <chrono>
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
