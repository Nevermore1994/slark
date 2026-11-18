//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#include "HttpStream.h"

#include <arpa/inet.h>
#include <gtest/gtest.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <limits>
#include <memory>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include "MockHttpServer.h"
#include "TaskThread.h"

using namespace slark;
using slark::test::kMockBodySize;
using slark::test::MockHttpServer;
using slark::test::StallingHttpServer;

namespace {

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
    params.onData = [](DataChunk) {
    };
    params.onComplete = [](uint64_t) {
    };
    params.onError = [](Error, int32_t) {
    };
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
    EXPECT_TRUE(waitUntil([&]() {
        return completed.load();
    }));
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

    EXPECT_TRUE(waitUntil([&]() {
        return stream_->state() == StreamState::Completed;
    }));
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

    EXPECT_TRUE(waitUntil([&]() {
        return stream_->info().isSeekable;
    }));

    auto seekResult = stream_->seek(32768, 7);
    ASSERT_TRUE(seekResult.has_value());
    EXPECT_EQ(stream_->position(), 32768u);
    EXPECT_TRUE(waitUntil([&]() {
        return stream_->state() == StreamState::Completed;
    }));
    EXPECT_TRUE(sawSeekGeneration.load(std::memory_order_acquire));
    EXPECT_EQ(stream_->state(), StreamState::Completed);
}

TEST_F(HttpStreamTest, H2b_SeekWorksWhen206OmitsAcceptRangesHeader) {
    MockHttpServer omitHeaderServer;
    ASSERT_TRUE(omitHeaderServer.start(true, /*chunkDelayMs=*/0, false));

    auto params = makeParams();
    params.uri = omitHeaderServer.url();
    std::atomic<uint64_t> totalBytes{0};
    params.onData = [&totalBytes](DataChunk chunk) {
        totalBytes.fetch_add(chunk.size(), std::memory_order_relaxed);
    };

    auto stream = std::make_shared<HttpStream>(ownerThread_);
    ASSERT_TRUE(stream->open(params).has_value());
    stream->start();

    EXPECT_TRUE(waitUntil([&]() {
        return stream->info().isSeekable;
    }));

    auto seekResult = stream->seek(32768, 0);
    ASSERT_TRUE(seekResult.has_value());
    EXPECT_TRUE(waitUntil([&]() {
        return stream->state() == StreamState::Completed;
    }));
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

    EXPECT_TRUE(waitUntil([&]() {
        return stream->state() == StreamState::Completed;
    }));
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

    EXPECT_TRUE(waitUntil([&]() {
        return stream->position() >= kMockBodySize;
    }));
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

    EXPECT_TRUE(waitUntil([&]() {
        return totalBytes.load() > 0;
    }));
    stream->pause();
    EXPECT_EQ(stream->state(), StreamState::Paused);
    auto pausedPos = stream->position();
    EXPECT_GT(pausedPos, 0u);
    EXPECT_LT(pausedPos, kMockBodySize);

    stream->start();
    EXPECT_TRUE(waitUntil([&]() {
        return stream->state() == StreamState::Completed;
    }));
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

    EXPECT_TRUE(waitUntil([&]() {
        return totalBytes.load() > 0;
    }));
    stream_->close();
    EXPECT_EQ(stream_->state(), StreamState::Idle);
}

TEST_F(HttpStreamTest, DestroyDuringTransferReleasesTheStream) {
    MockHttpServer slowServer;
    ASSERT_TRUE(slowServer.start(true, /*chunkDelayMs=*/2));

    auto params = makeParams();
    params.uri = slowServer.url();
    auto totalBytes = std::make_shared<std::atomic<uint64_t>>(0);
    params.onData = [totalBytes](DataChunk chunk) {
        totalBytes->fetch_add(chunk.size(), std::memory_order_relaxed);
    };

    auto stream = std::make_shared<HttpStream>(ownerThread_);
    std::weak_ptr<HttpStream> weak = stream;
    ASSERT_TRUE(stream->open(params).has_value());
    stream->start();
    ASSERT_TRUE(waitUntil([&]() { return totalBytes->load() > 0; }));

    stream.reset();
    EXPECT_TRUE(waitUntil([&]() { return weak.expired(); }, 5000));
    slowServer.stop();
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
    EXPECT_TRUE(waitUntil([&]() {
        return errored.load();
    }));
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
    EXPECT_TRUE(waitUntil([&]() {
        return errored.load();
    }));
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

    EXPECT_TRUE(waitUntil([&]() {
        return stream_->position() >= kSent;
    }));
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

        ASSERT_TRUE(waitUntil([&]() {
            return errorCount.load() > 0;
        })) << status;
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

    ASSERT_TRUE(waitUntil([&]() {
        return errored.load();
    }));
    EXPECT_TRUE(waitUntil(
        [&]() {
            return server.clientDisconnected();
        },
        3000))
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

    ASSERT_TRUE(waitUntil([&]() {
        return errored.load();
    }));
    EXPECT_EQ(stream_->state(), StreamState::Error);
    EXPECT_TRUE(waitUntil(
        [&]() {
            return server.clientDisconnected();
        },
        3000))
        << "the whole file must not keep downloading after Range was ignored";
}

TEST_F(HttpStreamTest, W1_WindowReuseKeepsEveryByteInOrder) {
    constexpr size_t kBodySize = 512 * 1024;
    std::string body(kBodySize, '\0');
    for (size_t i = 0; i < kBodySize; ++i) {
        body[i] = static_cast<char>('a' + i % 26);
    }

    MockHttpServer server;
    server.setBody(body);
    ASSERT_TRUE(server.start(true, /*chunkDelayMs=*/1));

    auto params = makeParams();
    params.uri = server.url();
    std::string received;
    received.reserve(kBodySize);
    std::atomic<bool> completed{false};
    params.onData = [&received](DataChunk chunk) {
        const auto view = chunk.data.toStringView();
        received.append(view.data(), view.size());
    };
    params.onComplete = [&completed](uint64_t) {
        completed = true;
    };

    ASSERT_TRUE(stream_->open(params).has_value());
    stream_->start();
    EXPECT_TRUE(waitUntil([&]() {
        return completed.load();
    }));
    EXPECT_EQ(received, body);
    EXPECT_EQ(stream_->state(), StreamState::Completed);
    server.stop();
}

TEST_F(HttpStreamTest, W2_PauseWhileWindowIsBackpressuredDoesNotDeadlock) {
    constexpr size_t kBodySize = 512 * 1024;
    MockHttpServer server;
    server.setBody(std::string(kBodySize, 'x'));
    ASSERT_TRUE(server.start(true, /*chunkDelayMs=*/1));

    auto params = makeParams();
    params.uri = server.url();
    std::atomic<uint64_t> totalBytes{0};
    params.onData = [&totalBytes](DataChunk chunk) {
        totalBytes.fetch_add(chunk.size(), std::memory_order_relaxed);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    };

    ASSERT_TRUE(stream_->open(params).has_value());
    stream_->start();
    ASSERT_TRUE(waitUntil([&]() {
        return totalBytes.load() > 0;
    }));
    stream_->pause();
    EXPECT_EQ(stream_->state(), StreamState::Paused);
    stream_->close();
    EXPECT_EQ(stream_->state(), StreamState::Idle);
    server.stop();
}
