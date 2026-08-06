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
#include <memory>
#include <string>
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

    bool start() {
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
            connThreads_.emplace_back([this, fd]() { handleConnection(fd); });
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

        std::string path;
        auto space1 = request.find(' ');
        auto space2 = request.find(' ', space1 + 1);
        if (space1 != std::string::npos && space2 != std::string::npos) {
            path = request.substr(space1 + 1, space2 - space1 - 1);
        }

        std::string response;
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
                       "seg1.ts\n"
                       "#EXT-X-ENDLIST\n";
        } else if (path == "/seg0.ts" || path == "/seg1.ts") {
            std::string body(1024, 'T');
            response = "HTTP/1.1 200 OK\r\n"
                       "Content-Type: video/mp2t\r\n"
                       "Content-Length: " +
                       std::to_string(body.size()) + "\r\n"
                       "Connection: close\r\n\r\n" +
                       body;
            sendAll(fd, response.data(), response.size());
            ::close(fd);
            return;
        } else {
            response = "HTTP/1.1 404 Not Found\r\n"
                       "Content-Length: 0\r\n"
                       "Connection: close\r\n\r\n";
            sendAll(fd, response.data(), response.size());
            ::close(fd);
            return;
        }

        std::string full = "HTTP/1.1 200 OK\r\n"
                           "Content-Type: application/vnd.apple.mpegurl\r\n"
                           "Content-Length: " +
                           std::to_string(response.size()) + "\r\n"
                           "Connection: close\r\n\r\n" +
                           response;
        sendAll(fd, full.data(), full.size());
        ::close(fd);
    }

    int listenFd_ = -1;
    int port_ = 0;
    std::atomic<bool> stopped_{false};
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
