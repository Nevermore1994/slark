//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#include <gtest/gtest.h>

#include <arpa/inet.h>
#include <cerrno>
#include <csignal>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "HttpStream.h"
#include "Thread.h"

using namespace slark;

namespace {

constexpr uint32_t kMockBodySize = 65536;

class MockHttpServer {
 public:
    ~MockHttpServer() {
        stop();
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
        thread_ = std::thread([this]() { run(); });
        return true;
    }

    [[nodiscard]] std::string url() const {
        return "http://127.0.0.1:" + std::to_string(port_) + "/file.bin";
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

        std::string body;
        if (hasRange && honorRange_ && rangeStart < kMockBodySize) {
            uint64_t end = kMockBodySize - 1;
            uint64_t bodyLen = kMockBodySize - rangeStart;
            std::string header = "HTTP/1.1 206 Partial Content\r\n"
                                 "Content-Type: application/octet-stream\r\n";
            if (emitAcceptRanges_) {
                header += "Accept-Ranges: bytes\r\n";
            }
            header += "Content-Range: bytes " + std::to_string(rangeStart) + "-" +
                      std::to_string(end) + "/" + std::to_string(kMockBodySize) + "\r\n"
                      "Content-Length: " +
                      std::to_string(bodyLen) + "\r\n"
                      "Connection: close\r\n\r\n";
            body.reserve(static_cast<size_t>(bodyLen));
            for (uint64_t i = 0; i < bodyLen; ++i) {
                body.push_back(static_cast<char>(i % 251));
            }
            sendAll(fd, header.data(), header.size());
            sendBodyChunked(fd, body.data(), body.size());
        } else {
            body.reserve(kMockBodySize);
            for (uint32_t i = 0; i < kMockBodySize; ++i) {
                body.push_back(static_cast<char>(i % 251));
            }
            std::string response = "HTTP/1.1 200 OK\r\n"
                                   "Content-Type: application/octet-stream\r\n"
                                   "Content-Length: " +
                                   std::to_string(body.size()) + "\r\n"
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
    std::atomic<bool> stopped_{false};
    std::thread thread_;
    std::vector<std::thread> connThreads_;
};

std::shared_ptr<Thread> makeOwnerThread() {
    auto thread = Thread::create("HttpStreamTest");
    thread->start();
    return thread;
}

class HttpStreamTest : public ::testing::Test {
 protected:
    void SetUp() override {
        ASSERT_TRUE(server_.start());
        ownerThread_ = makeOwnerThread();
        stream_ = std::make_shared<HttpStream>(ownerThread_);
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

    MockHttpServer server_;
    std::shared_ptr<Thread> ownerThread_;
    std::shared_ptr<HttpStream> stream_;
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

}

TEST_F(HttpStreamTest, H1_OpenStartReceivesDataAndCompletes) {
    auto params = makeParams();
    params.uri = server_.url();
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
    EXPECT_TRUE(waitUntil([&]() { return completed.load(); }));
    EXPECT_EQ(totalBytes.load(), kMockBodySize);
    EXPECT_EQ(stream_->state(), StreamState::Completed);
    EXPECT_TRUE(stream_->isCompleted());
}

TEST_F(HttpStreamTest, H2_SeekAfterHeaderSetsNewRange) {
    auto params = makeParams();
    params.uri = server_.url();
    std::atomic<uint64_t> totalBytes{0};
    params.onData = [&totalBytes](DataChunk chunk) {
        totalBytes.fetch_add(chunk.size(), std::memory_order_relaxed);
    };

    ASSERT_TRUE(stream_->open(params).has_value());
    stream_->start();

    EXPECT_TRUE(waitUntil([&]() { return stream_->info().isSeekable; }));

    auto seekResult = stream_->seek(32768);
    ASSERT_TRUE(seekResult.has_value());
    EXPECT_EQ(stream_->position(), 32768u);
    EXPECT_TRUE(waitUntil([&]() { return stream_->position() >= kMockBodySize; }));
    EXPECT_EQ(stream_->state(), StreamState::Completed);
}

TEST_F(HttpStreamTest, H2b_SeekWorksWhen206OmitsAcceptRangesHeader) {
    MockHttpServer omitHeaderServer;
    ASSERT_TRUE(omitHeaderServer.start(true, /*chunkDelayMs=*/0,
                                       false));

    auto params = makeParams();
    params.uri = omitHeaderServer.url();
    std::atomic<uint64_t> totalBytes{0};
    params.onData = [&totalBytes](DataChunk chunk) {
        totalBytes.fetch_add(chunk.size(), std::memory_order_relaxed);
    };

    auto stream = std::make_shared<HttpStream>(ownerThread_);
    ASSERT_TRUE(stream->open(params).has_value());
    stream->start();

    EXPECT_TRUE(waitUntil([&]() { return stream->info().isSeekable; }));

    auto seekResult = stream->seek(32768);
    ASSERT_TRUE(seekResult.has_value());
    EXPECT_TRUE(waitUntil([&]() { return stream->position() >= kMockBodySize; }));
    EXPECT_EQ(stream->state(), StreamState::Completed);
    stream->close();
    omitHeaderServer.stop();
}

TEST_F(HttpStreamTest, H2c_SequentialFallbackWhenServerIgnoresRange) {
    MockHttpServer noRangeServer;
    ASSERT_TRUE(noRangeServer.start(false));

    auto params = makeParams();
    params.uri = noRangeServer.url();
    std::atomic<uint64_t> totalBytes{0};
    std::atomic<bool> errored{false};
    params.onData = [&totalBytes](DataChunk chunk) {
        totalBytes.fetch_add(chunk.size(), std::memory_order_relaxed);
    };
    params.onError = [&errored](Error, int32_t) {
        errored = true;
    };

    auto stream = std::make_shared<HttpStream>(ownerThread_);
    ASSERT_TRUE(stream->open(params).has_value());
    stream->start();

    EXPECT_TRUE(waitUntil([&]() { return stream->position() >= kMockBodySize; }));
    EXPECT_EQ(stream->state(), StreamState::Completed);
    EXPECT_FALSE(errored.load());
    EXPECT_FALSE(stream->info().isSeekable);
    stream->close();
    noRangeServer.stop();
}

TEST_F(HttpStreamTest, H2d_SeekRejectedWhenServerIgnoresRange) {
    MockHttpServer noRangeServer;
    ASSERT_TRUE(noRangeServer.start(false));

    auto params = makeParams();
    params.uri = noRangeServer.url();
    std::atomic<bool> errored{false};
    params.onError = [&errored](Error, int32_t) {
        errored = true;
    };

    auto stream = std::make_shared<HttpStream>(ownerThread_);
    ASSERT_TRUE(stream->open(params).has_value());
    stream->start();

    EXPECT_TRUE(waitUntil([&]() { return stream->position() >= kMockBodySize; }));
    auto seekResult = stream->seek(1024);
    EXPECT_FALSE(seekResult.has_value());
    EXPECT_EQ(seekResult.error(), Error::StreamNotReady);
    stream->close();
    noRangeServer.stop();
}

TEST_F(HttpStreamTest, H3_PauseResumeResumesFromCommittedPosition) {
    MockHttpServer slowServer;
    ASSERT_TRUE(slowServer.start(true, /*chunkDelayMs=*/2));

    auto params = makeParams();
    params.uri = slowServer.url();
    std::atomic<uint64_t> totalBytes{0};
    params.onData = [&totalBytes](DataChunk chunk) {
        totalBytes.fetch_add(chunk.size(), std::memory_order_relaxed);
    };

    auto stream = std::make_shared<HttpStream>(ownerThread_);
    ASSERT_TRUE(stream->open(params).has_value());
    stream->start();

    EXPECT_TRUE(waitUntil([&]() { return totalBytes.load() > 0; }));
    stream->pause();
    EXPECT_EQ(stream->state(), StreamState::Paused);
    auto pausedPos = stream->position();
    EXPECT_GT(pausedPos, 0u);
    EXPECT_LT(pausedPos, kMockBodySize);

    stream->start();
    EXPECT_TRUE(waitUntil([&]() { return stream->position() >= kMockBodySize; }));
    EXPECT_EQ(stream->state(), StreamState::Completed);
    stream->close();
    slowServer.stop();
}

TEST_F(HttpStreamTest, H4_CloseDuringTransferIsSafe) {
    auto params = makeParams();
    params.uri = server_.url();
    std::atomic<uint64_t> totalBytes{0};
    params.onData = [&totalBytes](DataChunk chunk) {
        totalBytes.fetch_add(chunk.size(), std::memory_order_relaxed);
    };

    ASSERT_TRUE(stream_->open(params).has_value());
    stream_->start();

    EXPECT_TRUE(waitUntil([&]() { return totalBytes.load() > 0; }));
    stream_->close();
    EXPECT_EQ(stream_->state(), StreamState::Idle);
}

TEST_F(HttpStreamTest, H5_RangeValidationRejectsServerIgnoringRange) {
    MockHttpServer noRangeServer;
    ASSERT_TRUE(noRangeServer.start(false));

    auto params = makeParams();
    params.uri = noRangeServer.url();
    params.range = Range(1024);
    std::atomic<bool> errored{false};
    params.onError = [&errored](Error, int32_t) {
        errored = true;
    };

    auto stream = std::make_shared<HttpStream>(ownerThread_);
    ASSERT_TRUE(stream->open(params).has_value());
    stream->start();
    EXPECT_TRUE(waitUntil([&]() { return errored.load(); }));
    EXPECT_EQ(stream->state(), StreamState::Error);
    stream->close();
    noRangeServer.stop();
}

TEST_F(HttpStreamTest, H6_RangeValidationRejectsOutOfRange) {
    auto params = makeParams();
    params.uri = server_.url();
    params.range = Range(kMockBodySize + 100);
    std::atomic<bool> errored{false};
    params.onError = [&errored](Error, int32_t) {
        errored = true;
    };

    ASSERT_TRUE(stream_->open(params).has_value());
    stream_->start();
    EXPECT_TRUE(waitUntil([&]() { return errored.load(); }));
    EXPECT_EQ(stream_->state(), StreamState::Error);
}
