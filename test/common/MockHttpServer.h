//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//
#pragma once

#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace slark::test {

inline constexpr uint32_t kMockBodySize = 65536;

class MockHttpServer {
 public:
    ~MockHttpServer() {
        stop();
    }

    // Serves these bytes instead of the synthetic pattern. Must be set before start().
    void setBody(std::string body) {
        body_ = std::move(body);
    }

    // The Range start of every request so far ("" when the request had no Range header).
    [[nodiscard]] std::vector<std::string> rangeRequests() const {
        std::lock_guard lock(requestsMutex_);
        return rangeRequests_;
    }

    [[nodiscard]] int requestCount() const {
        return requestCount_.load();
    }

    // Answers every request with this status line and an empty body, e.g. "404 Not Found".
    void failWith(std::string statusLine) {
        failStatus_ = std::move(statusLine);
    }

    bool start(bool honorRange = true, int32_t chunkDelayMs = 0, bool emitAcceptRanges = true) {
        honorRange_ = honorRange;
        chunkDelayMs_ = chunkDelayMs;
        emitAcceptRanges_ = emitAcceptRanges;
        listenFd_ = ::socket(AF_INET, SOCK_STREAM, 0);
        if (listenFd_ < 0) {
            return false;
        }
        int noSigpipe = 1;
        ::setsockopt(listenFd_, SOL_SOCKET, SO_NOSIGPIPE, &noSigpipe, sizeof(noSigpipe));
        int reuse = 1;
        ::setsockopt(listenFd_, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        addr.sin_port = 0;
        if (::bind(listenFd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
            return false;
        }
        socklen_t len = sizeof(addr);
        if (::getsockname(listenFd_, reinterpret_cast<sockaddr*>(&addr), &len) < 0) {
            return false;
        }
        port_ = ntohs(addr.sin_port);
        if (::listen(listenFd_, 8) < 0) {
            return false;
        }
        thread_ = std::thread([this]() {
            run();
        });
        return true;
    }

    [[nodiscard]] std::string url() const {
        return "http://127.0.0.1:" + std::to_string(port_) + "/file.bin";
    }

    // The accept thread reads listenFd_ and writes connThreads_ while it runs, so both are only
    // touched here after it has been joined.
    void stop() {
        stopped_ = true;
        if (thread_.joinable()) {
            thread_.join();
        }
        if (listenFd_ >= 0) {
            ::close(listenFd_);
            listenFd_ = -1;
        }
        for (auto& t : connThreads_) {
            if (t.joinable()) {
                t.join();
            }
        }
        connThreads_.clear();
    }

 private:
    static void sendAll(int fd, const char* data, size_t size) {
        size_t sent = 0;
        while (sent < size) {
            auto n = ::send(fd, data + sent, size - sent, 0);
            if (n <= 0) {
                break;
            }
            sent += static_cast<size_t>(n);
        }
    }

    void sendBodyChunked(int fd, const char* data, size_t size) {
        constexpr size_t kChunk = 4096;
        size_t sent = 0;
        while (sent < size) {
            auto n = std::min(kChunk, size - sent);
            sendAll(fd, data + sent, n);
            sent += n;
            if (chunkDelayMs_ > 0 && sent < size) {
                std::this_thread::sleep_for(std::chrono::milliseconds(chunkDelayMs_));
            }
        }
    }

    // shutdown() does not wake a thread blocked in accept() on every platform, so the accept
    // loop polls with a timeout and checks stopped_ itself.
    bool waitForClient() const {
        pollfd p{listenFd_, POLLIN, 0};
        return ::poll(&p, 1, 20) > 0;
    }

    void run() {
        while (!stopped_) {
            if (!waitForClient()) {
                continue;
            }
            int fd = ::accept(listenFd_, nullptr, nullptr);
            if (fd < 0) {
                continue;
            }
            connThreads_.emplace_back([this, fd]() {
                handleConnection(fd);
            });
        }
    }

    void handleConnection(int fd) {
        int noSigpipe = 1;
        ::setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &noSigpipe, sizeof(noSigpipe));
        std::string request;
        char buf[1024];
        while (true) {
            auto n = ::recv(fd, buf, sizeof(buf), 0);
            if (n <= 0) {
                break;
            }
            request.append(buf, static_cast<size_t>(n));
            if (request.find("\r\n\r\n") != std::string::npos) {
                break;
            }
        }

        uint64_t rangeStart = 0;
        bool hasRange = false;
        auto rangePos = request.find("Range: bytes=");
        if (rangePos != std::string::npos) {
            auto startPos = rangePos + std::string("Range: bytes=").size();
            auto dashPos = request.find('-', startPos);
            if (dashPos != std::string::npos) {
                auto numStr = request.substr(startPos, dashPos - startPos);
                rangeStart = static_cast<uint64_t>(std::stoull(numStr));
                hasRange = true;
            }
        }

        ++requestCount_;
        {
            std::lock_guard lock(requestsMutex_);
            rangeRequests_.push_back(hasRange ? "bytes=" + std::to_string(rangeStart) : "");
        }
        if (!failStatus_.empty()) {
            const std::string response =
                "HTTP/1.1 " + failStatus_ + "\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
            sendAll(fd, response.data(), response.size());
            ::close(fd);
            return;
        }

        // The synthetic pattern is relative to the start of each response, which the older
        // tests rely on; a file body is addressed by its real offset.
        const uint64_t total = body_.empty() ? kMockBodySize : body_.size();
        const auto byteAt = [&](uint64_t responseIndex, uint64_t fileOffset) {
            return body_.empty() ? static_cast<char>(responseIndex % 251)
                                 : body_[static_cast<size_t>(fileOffset)];
        };

        std::string body;
        if (hasRange && honorRange_ && rangeStart < total) {
            uint64_t end = total - 1;
            uint64_t bodyLen = total - rangeStart;
            std::string header = "HTTP/1.1 206 Partial Content\r\n"
                                 "Content-Type: application/octet-stream\r\n";
            if (emitAcceptRanges_) {
                header += "Accept-Ranges: bytes\r\n";
            }
            header += "Content-Range: bytes " + std::to_string(rangeStart) + "-" +
                      std::to_string(end) + "/" + std::to_string(total) +
                      "\r\n"
                      "Content-Length: " +
                      std::to_string(bodyLen) +
                      "\r\n"
                      "Connection: close\r\n\r\n";
            body.reserve(static_cast<size_t>(bodyLen));
            for (uint64_t i = 0; i < bodyLen; ++i) {
                body.push_back(byteAt(i, rangeStart + i));
            }
            sendAll(fd, header.data(), header.size());
            sendBodyChunked(fd, body.data(), body.size());
        } else {
            body.reserve(static_cast<size_t>(total));
            for (uint64_t i = 0; i < total; ++i) {
                body.push_back(byteAt(i, i));
            }
            std::string response = "HTTP/1.1 200 OK\r\n"
                                   "Content-Type: application/octet-stream\r\n"
                                   "Content-Length: " +
                                   std::to_string(body.size()) +
                                   "\r\n"
                                   "Connection: close\r\n\r\n" +
                                   body;
            sendBodyChunked(fd, response.data(), response.size());
        }
        ::close(fd);
    }

    int listenFd_ = -1;
    int port_ = 0;
    bool honorRange_ = true;
    bool emitAcceptRanges_ = true;
    int32_t chunkDelayMs_ = 0;
    std::string body_;
    std::string failStatus_;
    std::atomic<int> requestCount_{0};
    mutable std::mutex requestsMutex_;
    std::vector<std::string> rangeRequests_;
    std::atomic<bool> stopped_{false};
    std::thread thread_;
    std::vector<std::thread> connThreads_;
};

// Announces announcedBodySize bytes, sends only sentBodySize of them, then holds the connection
// open until the client disconnects, so a client that keeps downloading is observable.
class StallingHttpServer {
 public:
    ~StallingHttpServer() {
        stop();
    }

    bool start(std::string statusLine, uint64_t announcedBodySize, size_t sentBodySize) {
        statusLine_ = std::move(statusLine);
        announcedBodySize_ = announcedBodySize;
        sentBodySize_ = sentBodySize;
        listenFd_ = ::socket(AF_INET, SOCK_STREAM, 0);
        if (listenFd_ < 0) {
            return false;
        }
        int reuse = 1;
        ::setsockopt(listenFd_, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        addr.sin_port = 0;
        if (::bind(listenFd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
            return false;
        }
        socklen_t len = sizeof(addr);
        if (::getsockname(listenFd_, reinterpret_cast<sockaddr*>(&addr), &len) < 0 ||
            ::listen(listenFd_, 4) < 0) {
            return false;
        }
        port_ = ntohs(addr.sin_port);
        thread_ = std::thread([this]() {
            serve();
        });
        return true;
    }

    void stop() {
        stopped_ = true;
        if (thread_.joinable()) {
            thread_.join();
        }
        if (listenFd_ >= 0) {
            ::close(listenFd_);
            listenFd_ = -1;
        }
    }

    [[nodiscard]] std::string url() const {
        return "http://127.0.0.1:" + std::to_string(port_) + "/file.bin";
    }

    [[nodiscard]] bool clientDisconnected() const {
        return clientDisconnected_.load();
    }

 private:
    void serve() {
        while (!stopped_) {
            pollfd p{listenFd_, POLLIN, 0};
            if (::poll(&p, 1, 20) > 0) {
                break;
            }
        }
        if (stopped_) {
            return;
        }
        int fd = ::accept(listenFd_, nullptr, nullptr);
        if (fd < 0) {
            return;
        }
        int noSigpipe = 1;
        ::setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &noSigpipe, sizeof(noSigpipe));
        std::string request;
        char buf[1024];
        while (request.find("\r\n\r\n") == std::string::npos) {
            auto n = ::recv(fd, buf, sizeof(buf), 0);
            if (n <= 0) {
                ::close(fd);
                return;
            }
            request.append(buf, static_cast<size_t>(n));
        }

        std::string response = "HTTP/1.1 " + statusLine_ +
                               "\r\nContent-Type: application/octet-stream\r\n"
                               "Content-Length: " +
                               std::to_string(announcedBodySize_) +
                               "\r\nConnection: close\r\n\r\n" + std::string(sentBodySize_, 'x');
        size_t sent = 0;
        while (sent < response.size()) {
            auto n = ::send(fd, response.data() + sent, response.size() - sent, 0);
            if (n <= 0) {
                break;
            }
            sent += static_cast<size_t>(n);
        }

        // recv() returns 0 once the client closes its end
        timeval timeout{0, 100000};
        ::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
        while (!stopped_) {
            auto n = ::recv(fd, buf, sizeof(buf), 0);
            if (n == 0 || (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK)) {
                clientDisconnected_ = true;
                break;
            }
        }
        ::close(fd);
    }

    std::string statusLine_;
    uint64_t announcedBodySize_ = 0;
    size_t sentBodySize_ = 0;
    int listenFd_ = -1;
    int port_ = 0;
    std::atomic<bool> stopped_{false};
    std::atomic<bool> clientDisconnected_{false};
    std::thread thread_;
};

}  // namespace slark::test
