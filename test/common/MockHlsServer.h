//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//
#pragma once

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <charconv>
#include <chrono>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

namespace slark::test {

class MockHlsServer {
 public:
    ~MockHlsServer() {
        stop();
    }

    bool start(bool keepAlive = true) {
        keepAlive_ = keepAlive;
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

    [[nodiscard]] std::string masterUrl() const {
        return "http://127.0.0.1:" + std::to_string(port_) + "/master.m3u8";
    }

    [[nodiscard]] int connectionCount() const noexcept {
        return connectionCount_.load(std::memory_order_acquire);
    }

    [[nodiscard]] int requestCount(std::string_view path) const {
        std::lock_guard<std::mutex> lock(requestMutex_);
        const auto it = requestCounts_.find(std::string(path));
        return it == requestCounts_.end() ? 0 : it->second;
    }

    void setLive(bool live) noexcept {
        live_.store(live, std::memory_order_release);
    }

    void setTargetDuration(uint32_t seconds) noexcept {
        targetDuration_.store(seconds, std::memory_order_release);
    }

    void setKeepAlive(bool keepAlive) noexcept {
        keepAlive_ = keepAlive;
    }

    void failFirstSegment() noexcept {
        failFirstSegment_.store(true, std::memory_order_release);
    }

    void failNextMediaPlaylist() noexcept {
        mediaPlaylistFailures_.fetch_add(1, std::memory_order_release);
    }

    void setSlidingLive(bool sliding) noexcept {
        slidingLive_.store(sliding, std::memory_order_release);
    }

    void setByteRangePlaylist(bool enabled) noexcept {
        byteRangePlaylist_.store(enabled, std::memory_order_release);
    }

    void setIgnoreRange(bool enabled) noexcept {
        ignoreRange_.store(enabled, std::memory_order_release);
    }

    [[nodiscard]] std::string rangeHeader(std::string_view path, size_t index = 0) const {
        std::lock_guard<std::mutex> lock(requestMutex_);
        const auto it = rangeHeaders_.find(std::string(path));
        if (it == rangeHeaders_.end() || index >= it->second.size()) {
            return {};
        }
        return it->second[index];
    }

    // Overrides the built-in media.m3u8 body; the argument is the request count seen so far.
    void setMediaPlaylistFactory(std::function<std::string(int)> factory) noexcept {
        mediaPlaylistFactory_ = std::move(factory);
    }

    // Serves any path with a fixed body, e.g. a real TS sample split into several segments.
    void setResource(std::string path, std::string body, std::string contentType) {
        resources_[std::move(path)] = {std::move(body), std::move(contentType)};
    }

    void stop() {
        stopped_ = true;
        if (listenFd_ >= 0) {
            ::shutdown(listenFd_, SHUT_RDWR);
            ::close(listenFd_);
        }
        if (thread_.joinable()) {
            thread_.join();
        }
        listenFd_ = -1;
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

    static std::string headerValue(std::string_view request, std::string_view name) {
        size_t lineStart = 0;
        while (lineStart < request.size()) {
            const auto lineEnd = request.find("\r\n", lineStart);
            const auto line = request.substr(lineStart, lineEnd == std::string_view::npos
                                                            ? request.size() - lineStart
                                                            : lineEnd - lineStart);
            const auto colon = line.find(':');
            if (colon != std::string_view::npos && colon == name.size() &&
                std::ranges::equal(line.substr(0, colon), name,
                                   [](unsigned char lhs, unsigned char rhs) {
                                       return std::tolower(lhs) == std::tolower(rhs);
                                   })) {
                auto value = line.substr(colon + 1);
                while (!value.empty() && value.front() == ' ') {
                    value.remove_prefix(1);
                }
                return std::string(value);
            }
            if (lineEnd == std::string_view::npos) {
                break;
            }
            lineStart = lineEnd + 2;
        }
        return {};
    }

    void run() {
        while (!stopped_) {
            int fd = ::accept(listenFd_, nullptr, nullptr);
            if (fd < 0) {
                if (stopped_) {
                    break;
                }
                continue;
            }
            connectionCount_.fetch_add(1, std::memory_order_release);
            connThreads_.emplace_back([this, fd]() {
                handleConnection(fd);
            });
        }
    }

    void handleConnection(int fd) {
        int noSigpipe = 1;
        ::setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &noSigpipe, sizeof(noSigpipe));
        timeval receiveTimeout{0, 100000};
        ::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &receiveTimeout, sizeof(receiveTimeout));

        while (!stopped_) {
            std::string request;
            char buf[1024];
            while (request.find("\r\n\r\n") == std::string::npos) {
                auto n = ::recv(fd, buf, sizeof(buf), 0);
                if (n <= 0) {
                    if (!stopped_ && (errno == EAGAIN || errno == EWOULDBLOCK)) {
                        continue;
                    }
                    ::close(fd);
                    return;
                }
                request.append(buf, static_cast<size_t>(n));
            }

            std::string path;
            auto space1 = request.find(' ');
            auto space2 = request.find(' ', space1 + 1);
            if (space1 != std::string::npos && space2 != std::string::npos) {
                path = request.substr(space1 + 1, space2 - space1 - 1);
            }
            int pathRequestCount = 0;
            const auto rangeHeader = headerValue(request, "Range");
            {
                std::lock_guard<std::mutex> lock(requestMutex_);
                pathRequestCount = ++requestCounts_[path];
                rangeHeaders_[path].push_back(rangeHeader);
            }

            std::string response;
            std::string contentType = "application/vnd.apple.mpegurl";
            int statusCode = 200;
            if (auto it = resources_.find(path); it != resources_.end()) {
                contentType = it->second.second;
                response = it->second.first;
            } else if (path == "/master.m3u8") {
                response = "#EXTM3U\n"
                           "#EXT-X-STREAM-INF:BANDWIDTH=800000\n"
                           "media.m3u8\n";
            } else if (path == "/media.m3u8" && mediaPlaylistFactory_) {
                response = mediaPlaylistFactory_(pathRequestCount);
            } else if (path == "/media.m3u8") {
                const bool sliding = slidingLive_.load(std::memory_order_acquire);
                const bool byteRange = byteRangePlaylist_.load(std::memory_order_acquire);
                const bool shifted = sliding && pathRequestCount > 1;
                const int sequence = shifted ? 101 : (sliding ? 100 : 0);
                const char* firstSegment =
                    shifted ? "seg101.ts" : (sliding ? "seg100.ts" : "seg0.ts");
                const char* secondSegment =
                    shifted ? "seg102.ts" : (sliding ? "seg101.ts" : "seg1.ts");
                response = "#EXTM3U\n"
                           "#EXT-X-VERSION:3\n"
                           "#EXT-X-TARGETDURATION:" +
                           std::to_string(targetDuration_.load(std::memory_order_acquire)) + "\n";
                response += "#EXT-X-MEDIA-SEQUENCE:" + std::to_string(sequence) + "\n";
                if (byteRange) {
                    response += "#EXTINF:1.0,\n#EXT-X-BYTERANGE:256@0\nasset.ts\n";
                    response += "#EXTINF:1.0,\n#EXT-X-BYTERANGE:256\nasset.ts\n";
                } else {
                    response += "#EXTINF:1.0,\n";
                    response += firstSegment;
                    response += "\n#EXTINF:1.0,\n";
                    response += secondSegment;
                    response += "\n";
                }
                if (!live_.load(std::memory_order_acquire)) {
                    response += "#EXT-X-ENDLIST\n";
                }
            } else if (path == "/seg0.ts" || path == "/seg1.ts" || path == "/seg100.ts" ||
                       path == "/seg101.ts" || path == "/seg102.ts" || path == "/asset.ts") {
                contentType = "video/mp2t";
                response.assign(1024, 'T');
            } else {
                statusCode = 404;
                response.clear();
            }

            std::string contentRange;
            if (path == "/asset.ts" && rangeHeader.starts_with("bytes=") &&
                !ignoreRange_.load(std::memory_order_acquire)) {
                const auto range = rangeHeader.substr(6);
                const auto dash = range.find('-');
                uint64_t start = 0;
                uint64_t end = 0;
                bool validRange = dash != std::string::npos;
                std::from_chars_result startResult{};
                std::from_chars_result endResult{};
                if (validRange) {
                    startResult = std::from_chars(range.data(), range.data() + dash, start);
                    endResult =
                        std::from_chars(range.data() + dash + 1, range.data() + range.size(), end);
                    validRange = startResult.ec == std::errc{} && endResult.ec == std::errc{} &&
                                 start <= end && end < response.size();
                }
                if (validRange) {
                    contentRange = "bytes " + std::to_string(start) + "-" + std::to_string(end) +
                                   "/" + std::to_string(response.size());
                    response = response.substr(start, end - start + 1);
                    statusCode = 206;
                } else {
                    statusCode = 416;
                    response.clear();
                }
            }

            bool failMediaPlaylist = false;
            auto remainingFailures = mediaPlaylistFailures_.load(std::memory_order_acquire);
            while (path == "/media.m3u8" && remainingFailures > 0 &&
                   !mediaPlaylistFailures_.compare_exchange_weak(
                       remainingFailures, remainingFailures - 1, std::memory_order_acq_rel)) {
            }
            failMediaPlaylist = path == "/media.m3u8" && remainingFailures > 0;
            if (failMediaPlaylist) {
                statusCode = 503;
                response.clear();
            }

            const bool truncatedSegment =
                path == "/seg0.ts" && failFirstSegment_.exchange(false, std::memory_order_acq_rel);
            const bool closeConnection =
                truncatedSegment || failMediaPlaylist || !keepAlive_ || statusCode >= 400;
            const std::string statusText = statusCode == 200   ? "OK"
                                           : statusCode == 206 ? "Partial Content"
                                           : statusCode == 404 ? "Not Found"
                                           : statusCode == 416 ? "Range Not Satisfiable"
                                                               : "Service Unavailable";
            std::string headers = "HTTP/1.1 " + std::to_string(statusCode) + " " + statusText +
                                  "\r\n" + "Content-Type: " + contentType + "\r\n" +
                                  "Content-Length: " + std::to_string(response.size()) + "\r\n";
            if (!contentRange.empty()) {
                headers += "Content-Range: " + contentRange + "\r\n";
            }
            headers += std::string("Connection: ") + (closeConnection ? "close" : "keep-alive") +
                       "\r\n\r\n";
            sendAll(fd, headers.data(), headers.size());
            if (truncatedSegment) {
                const auto partialSize = response.size() / 2;
                sendAll(fd, response.data(), partialSize / 2);
                std::this_thread::sleep_for(std::chrono::milliseconds(20));
                sendAll(fd, response.data() + partialSize / 2, partialSize - partialSize / 2);
            } else {
                sendAll(fd, response.data(), response.size());
            }
            if (closeConnection) {
                break;
            }
        }
        ::close(fd);
    }

    int listenFd_ = -1;
    int port_ = 0;
    bool keepAlive_ = true;
    std::atomic<bool> live_{false};
    std::atomic<uint32_t> targetDuration_{2};
    std::atomic<bool> failFirstSegment_{false};
    std::atomic<int> mediaPlaylistFailures_{0};
    std::atomic<bool> slidingLive_{false};
    std::atomic<bool> byteRangePlaylist_{false};
    std::atomic<bool> ignoreRange_{false};
    std::atomic<bool> stopped_{false};
    std::atomic<int> connectionCount_{0};
    mutable std::mutex requestMutex_;
    std::unordered_map<std::string, int> requestCounts_;
    std::unordered_map<std::string, std::vector<std::string>> rangeHeaders_;
    std::function<std::string(int)> mediaPlaylistFactory_;
    std::unordered_map<std::string, std::pair<std::string, std::string>> resources_;
    std::thread thread_;
    std::vector<std::thread> connThreads_;
};

}  // namespace slark::test
