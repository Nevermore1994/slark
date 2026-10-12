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
#include <limits>
#include <memory>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include "HttpStream.h"
#include "TaskThread.h"

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
        thread_ = std::thread([this]() { serve(); });
        return true;
    }

    void stop() {
        stopped_ = true;
        if (listenFd_ >= 0) {
            ::shutdown(listenFd_, SHUT_RDWR);
            ::close(listenFd_);
            listenFd_ = -1;
        }
        if (thread_.joinable()) {
            thread_.join();
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

std::shared_ptr<TaskThread> makeOwnerThread() {
    auto thread = TaskThread::create("HttpStreamTest");
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
    std::shared_ptr<TaskThread> ownerThread_;
    std::shared_ptr<HttpStream> stream_;
};

OpenParams makeParams() {
    OpenParams params;
    params.onData = [](DataChunk) {};
    params.onComplete = [](uint64_t) {};
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
    params.onComplete = [&completed](uint64_t) {
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

TEST_F(HttpStreamTest, H2_RangeCompletionDoesNotSignalSourceEof) {
    auto params = makeParams();
    params.uri = server_.url();
    params.range = Range(32768);
    std::atomic<bool> completed{false};
    params.onComplete = [&completed](uint64_t) {
        completed = true;
    };

    ASSERT_TRUE(stream_->open(params).has_value());
    stream_->start();

    EXPECT_TRUE(waitUntil([&]() { return stream_->position() >= kMockBodySize; }));
    EXPECT_EQ(stream_->state(), StreamState::Completed);
    EXPECT_FALSE(stream_->isCompleted());
    EXPECT_FALSE(completed.load());
}

TEST_F(HttpStreamTest, H2_SeekAfterHeaderSetsNewRange) {
    auto params = makeParams();
    params.uri = server_.url();
    std::atomic<uint64_t> totalBytes{0};
    std::atomic<bool> sawSeekGeneration{false};
    params.onData = [&totalBytes, &sawSeekGeneration](DataChunk chunk) {
        totalBytes.fetch_add(chunk.size(), std::memory_order_relaxed);
        if (chunk.sourceGeneration == 7) {
            sawSeekGeneration.store(true, std::memory_order_release);
        }
    };

    ASSERT_TRUE(stream_->open(params).has_value());
    stream_->start();

    EXPECT_TRUE(waitUntil([&]() { return stream_->info().isSeekable; }));

    auto seekResult = stream_->seek(32768, 7);
    ASSERT_TRUE(seekResult.has_value());
    EXPECT_EQ(stream_->position(), 32768u);
    EXPECT_TRUE(waitUntil([&]() { return stream_->position() >= kMockBodySize; }));
    EXPECT_TRUE(sawSeekGeneration.load(std::memory_order_acquire));
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

    auto seekResult = stream->seek(32768, 0);
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
    auto seekResult = stream->seek(1024, 0);
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

TEST_F(HttpStreamTest, R1_RequestInfoHasNoTotalDeadlineOrSizeCap) {
    const auto info = HttpStream::makeRequestInfo("http://example.com/a.mp4", Range{});
    EXPECT_EQ(info.methodType, http::HttpMethodType::Get);
    EXPECT_GE(info.readTimeout, std::chrono::hours(1));
    EXPECT_GT(info.readIdleTimeout, std::chrono::seconds(0));
    EXPECT_LE(info.readIdleTimeout, std::chrono::seconds(60));
    EXPECT_EQ(info.maxResponseBodySize, std::numeric_limits<uint64_t>::max());
    EXPECT_EQ(info.headers.count("Range"), 0u);
}

TEST_F(HttpStreamTest, R2_RequestInfoCarriesRangeHeader) {
    const auto info = HttpStream::makeRequestInfo("http://example.com/a.mp4", Range(1024));
    ASSERT_EQ(info.headers.count("Range"), 1u);
    EXPECT_EQ(info.headers.at("Range"), Range(1024).toHeaderString());
}

TEST_F(HttpStreamTest, R3_BodyLargerThanDefaultLimitIsAccepted) {
    constexpr uint64_t kAnnounced = 80ULL * 1024 * 1024;
    constexpr size_t kSent = 128 * 1024;
    StallingHttpServer server;
    ASSERT_TRUE(server.start("200 OK", kAnnounced, kSent));

    auto params = makeParams();
    params.uri = server.url();
    std::atomic<bool> errored{false};
    params.onError = [&errored](Error, int32_t) {
        errored = true;
    };

    ASSERT_TRUE(stream_->open(params).has_value());
    stream_->start();

    EXPECT_TRUE(waitUntil([&]() { return stream_->position() >= kSent; }));
    EXPECT_FALSE(errored.load());
    EXPECT_EQ(stream_->state(), StreamState::Reading);
}

TEST_F(HttpStreamTest, R4_ErrorStatusIsReportedWithoutDeliveringBody) {
    for (const char* status : {"404 Not Found", "500 Internal Server Error"}) {
        StallingHttpServer server;
        ASSERT_TRUE(server.start(status, 4096, 2048));

        auto params = makeParams();
        params.uri = server.url();
        std::atomic<uint64_t> bytes{0};
        std::atomic<int> errorCount{0};
        std::atomic<int32_t> originCode{0};
        std::atomic<Error> errorCode{Error::Success};
        params.onData = [&bytes](DataChunk chunk) {
            bytes.fetch_add(chunk.size());
        };
        params.onError = [&](Error error, int32_t origin) {
            errorCode = error;
            originCode = origin;
            errorCount.fetch_add(1);
        };

        auto stream = std::make_shared<HttpStream>(ownerThread_);
        ASSERT_TRUE(stream->open(params).has_value());
        stream->start();

        ASSERT_TRUE(waitUntil([&]() { return errorCount.load() > 0; })) << status;
        EXPECT_EQ(errorCode.load(), Error::HttpError) << status;
        EXPECT_EQ(originCode.load(), std::string_view(status).starts_with("404") ? 404 : 500);
        EXPECT_EQ(bytes.load(), 0u) << status;
        EXPECT_EQ(stream->state(), StreamState::Error) << status;

        // Give any in-flight event time to arrive; the error must still be reported once
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
        EXPECT_EQ(errorCount.load(), 1) << status;
        stream->close();
    }
}

TEST_F(HttpStreamTest, R5_ErrorStatusStopsTheDownload) {
    StallingHttpServer server;
    ASSERT_TRUE(server.start("503 Service Unavailable", 64ULL * 1024 * 1024, 4096));

    auto params = makeParams();
    params.uri = server.url();
    std::atomic<bool> errored{false};
    params.onError = [&errored](Error, int32_t) {
        errored = true;
    };

    ASSERT_TRUE(stream_->open(params).has_value());
    stream_->start();

    ASSERT_TRUE(waitUntil([&]() { return errored.load(); }));
    EXPECT_TRUE(waitUntil([&]() { return server.clientDisconnected(); }, 3000))
        << "the connection must be dropped instead of draining the error page";
}

TEST_F(HttpStreamTest, R6_IgnoredRangeStopsTheDownload) {
    StallingHttpServer server;
    ASSERT_TRUE(server.start("200 OK", 64ULL * 1024 * 1024, 4096));

    auto params = makeParams();
    params.uri = server.url();
    params.range = Range(1024);
    std::atomic<bool> errored{false};
    params.onError = [&errored](Error, int32_t) {
        errored = true;
    };

    ASSERT_TRUE(stream_->open(params).has_value());
    stream_->start();

    ASSERT_TRUE(waitUntil([&]() { return errored.load(); }));
    EXPECT_EQ(stream_->state(), StreamState::Error);
    EXPECT_TRUE(waitUntil([&]() { return server.clientDisconnected(); }, 3000))
        << "the whole file must not keep downloading after Range was ignored";
}
