//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#include <gtest/gtest.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <cerrno>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <vector>

#include "HLSStream.h"
#include "Thread.h"

using namespace slark;

namespace {

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
        thread_ = std::thread([this]() { run(); });
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
        live_ = live;
    }

    void setKeepAlive(bool keepAlive) noexcept {
        keepAlive_ = keepAlive;
    }

    void failFirstSegment() noexcept {
        failFirstSegment_.store(true, std::memory_order_release);
    }

    void stop() {
        stopped_ = true;
        if (listenFd_ >= 0) {
            ::shutdown(listenFd_, SHUT_RDWR);
            ::close(listenFd_);
            listenFd_ = -1;
        }
        for (auto& t : connThreads_) {
            if (t.joinable()) {
                t.join();
            }
        }
        connThreads_.clear();
        if (thread_.joinable()) {
            thread_.join();
        }
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
            connThreads_.emplace_back([this, fd]() { handleConnection(fd); });
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
            {
                std::lock_guard<std::mutex> lock(requestMutex_);
                ++requestCounts_[path];
            }

            std::string response;
            std::string contentType = "application/vnd.apple.mpegurl";
            int statusCode = 200;
            if (path == "/master.m3u8") {
                response = "#EXTM3U\n"
                           "#EXT-X-STREAM-INF:BANDWIDTH=800000\n"
                           "media.m3u8\n";
            } else if (path == "/media.m3u8") {
                response = "#EXTM3U\n"
                           "#EXT-X-VERSION:3\n"
                           "#EXT-X-TARGETDURATION:2\n"
                           "#EXT-X-MEDIA-SEQUENCE:0\n"
                           "#EXTINF:1.0,\n"
                           "seg0.ts\n"
                           "#EXTINF:1.0,\n"
                           "seg1.ts\n";
                if (!live_) {
                    response += "#EXT-X-ENDLIST\n";
                }
            } else if (path == "/seg0.ts" || path == "/seg1.ts") {
                contentType = "video/mp2t";
                response.assign(1024, 'T');
            } else {
                statusCode = 404;
                response.clear();
            }

            const bool truncatedSegment =
                path == "/seg0.ts" && failFirstSegment_.exchange(false, std::memory_order_acq_rel);
            const bool closeConnection = truncatedSegment || !keepAlive_ || statusCode != 200;
            const std::string statusText = statusCode == 200 ? "OK" : "Not Found";
            const std::string headers =
                "HTTP/1.1 " + std::to_string(statusCode) + " " + statusText + "\r\n" +
                "Content-Type: " + contentType + "\r\n" +
                "Content-Length: " + std::to_string(response.size()) + "\r\n" +
                "Connection: " + (closeConnection ? "close" : "keep-alive") + "\r\n\r\n";
            sendAll(fd, headers.data(), headers.size());
            if (!truncatedSegment) {
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
    bool live_ = false;
    std::atomic<bool> failFirstSegment_{false};
    std::atomic<bool> stopped_{false};
    std::atomic<int> connectionCount_{0};
    mutable std::mutex requestMutex_;
    std::unordered_map<std::string, int> requestCounts_;
    std::thread thread_;
    std::vector<std::thread> connThreads_;
};

std::shared_ptr<Thread> makeOwnerThread() {
    auto thread = Thread::create("HLSStreamTest");
    thread->start();
    return thread;
}

class HLSStreamTest : public ::testing::Test {
 protected:
    void SetUp() override {
        ASSERT_TRUE(server_.start());
        ownerThread_ = makeOwnerThread();
        stream_ = std::make_shared<HLSStream>(ownerThread_);
    }

    void TearDown() override {
        if (stream_) {
            stream_->close();
        }
        if (ownerThread_) {
            ownerThread_->stop();
            ownerThread_->join();
        }
        server_.stop();
    }

    MockHlsServer server_;
    std::shared_ptr<Thread> ownerThread_;
    std::shared_ptr<HLSStream> stream_;
};

OpenParams makeParams() {
    OpenParams params;
    params.onData = [](DataChunk) {};
    params.onComplete = [] {};
    params.onError = [](Error, int32_t) {};
    return params;
}

bool waitUntil(const std::function<bool()>& cond, int32_t maxMs = 10000) {
    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(maxMs);
    while (std::chrono::steady_clock::now() < deadline) {
        if (cond()) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return cond();
}

}  // namespace

TEST_F(HLSStreamTest, L1_MasterVariantMediaSegmentFlow) {
    auto params = makeParams();
    params.uri = server_.masterUrl();
    std::atomic<uint64_t> totalBytes{0};
    std::atomic<bool> completed{false};
    params.onData = [&totalBytes](DataChunk chunk) {
        totalBytes.fetch_add(chunk.size(), std::memory_order_relaxed);
    };
    params.onComplete = [&completed]() {
        completed = true;
    };

    auto result = stream_->open(params);
    ASSERT_TRUE(result.has_value());

    stream_->start();
    EXPECT_TRUE(waitUntil([&]() { return completed.load(); }, 15000));
    EXPECT_TRUE(stream_->isCompleted());
    EXPECT_EQ(stream_->state(), StreamState::Completed);
    EXPECT_EQ(totalBytes.load(), 2048u);
    EXPECT_EQ(server_.requestCount("/master.m3u8"), 1);
    EXPECT_EQ(server_.requestCount("/media.m3u8"), 1);
    EXPECT_EQ(server_.requestCount("/seg0.ts"), 1);
    EXPECT_EQ(server_.requestCount("/seg1.ts"), 1);
    EXPECT_EQ(server_.connectionCount(), 2);
}

TEST_F(HLSStreamTest, SegmentReconnectsAfterTruncatedResponse) {
    server_.failFirstSegment();
    auto params = makeParams();
    params.uri = server_.masterUrl();
    std::atomic<bool> completed{false};
    params.onComplete = [&completed]() {
        completed = true;
    };

    ASSERT_TRUE(stream_->open(params).has_value());
    stream_->start();

    EXPECT_TRUE(waitUntil([&]() { return completed.load(); }, 15000));
    EXPECT_TRUE(stream_->isCompleted());
    EXPECT_GE(server_.requestCount("/seg0.ts"), 2);
    EXPECT_EQ(server_.requestCount("/seg1.ts"), 1);
    EXPECT_GE(server_.connectionCount(), 3);
}

TEST_F(HLSStreamTest, LivePlaylistRefreshReusesControlSession) {
    server_.setLive(true);
    server_.setKeepAlive(true);
    auto params = makeParams();
    params.uri = server_.masterUrl();

    ASSERT_TRUE(stream_->open(params).has_value());
    stream_->start();

    EXPECT_TRUE(waitUntil([&]() { return server_.requestCount("/media.m3u8") >= 2; }, 7000));
    EXPECT_FALSE(stream_->isCompleted());
    EXPECT_EQ(server_.requestCount("/master.m3u8"), 1);
    EXPECT_EQ(server_.requestCount("/media.m3u8"), 2);
    EXPECT_EQ(server_.connectionCount(), 2);
}

TEST_F(HLSStreamTest, L3_SeekByTimeRestartsFromTargetSegment) {
    auto params = makeParams();
    params.uri = server_.masterUrl();
    std::atomic<uint64_t> totalBytes{0};
    std::atomic<bool> completed{false};
    params.onData = [&totalBytes](DataChunk chunk) {
        totalBytes.fetch_add(chunk.size(), std::memory_order_relaxed);
    };
    params.onComplete = [&completed]() {
        completed = true;
    };

    ASSERT_TRUE(stream_->open(params).has_value());
    stream_->start();
    EXPECT_TRUE(waitUntil([&]() { return totalBytes.load() > 0; }, 15000));

    auto res = stream_->seekByTime(1.5);
    ASSERT_TRUE(res.has_value());
    EXPECT_TRUE(waitUntil([&]() { return completed.load(); }, 15000));
    EXPECT_TRUE(stream_->isCompleted());
}

TEST_F(HLSStreamTest, L5_ConcurrentStartSeekCloseIsSafe) {
    auto params = makeParams();
    params.uri = server_.masterUrl();
    std::atomic<uint64_t> totalBytes{0};
    params.onData = [&totalBytes](DataChunk chunk) {
        totalBytes.fetch_add(chunk.size(), std::memory_order_relaxed);
    };

    ASSERT_TRUE(stream_->open(params).has_value());

    std::thread t1([this]() {
        for (int i = 0; i < 5; ++i) {
            stream_->start();
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
    });
    std::thread t2([this]() {
        for (int i = 0; i < 5; ++i) {
            stream_->seekByTime(0.5);
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
    });
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    t1.join();
    t2.join();
    stream_->close();
    EXPECT_TRUE(true);
}
