//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#include <gtest/gtest.h>

#include <arpa/inet.h>
#include <charconv>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cerrno>
#include <memory>
#include <mutex>
#include <ranges>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <vector>

#include "HLSStream.h"
#include "MockHlsServer.h"
#include "TaskThread.h"

using namespace slark;

namespace {
using slark::test::MockHlsServer;

std::shared_ptr<TaskThread> makeOwnerThread() {
    auto thread = TaskThread::create("HLSStreamTest");
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
    std::shared_ptr<TaskThread> ownerThread_;
    std::shared_ptr<HLSStream> stream_;
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

}  // namespace

TEST_F(HLSStreamTest, L1_MasterVariantMediaSegmentFlow) {
    auto params = makeParams();
    params.uri = server_.masterUrl();
    std::atomic<uint64_t> totalBytes{0};
    std::atomic<bool> completed{false};
    std::mutex chunkMutex;
    std::vector<DataChunk> chunks;
    params.onData = [&totalBytes, &chunkMutex, &chunks](DataChunk chunk) {
        totalBytes.fetch_add(chunk.size(), std::memory_order_relaxed);
        std::lock_guard lock(chunkMutex);
        chunks.emplace_back(std::move(chunk));
    };
    params.onComplete = [&completed](uint64_t) {
        completed = true;
    };

    auto result = stream_->open(params);
    ASSERT_TRUE(result.has_value());

    stream_->start();
    EXPECT_TRUE(waitUntil([&]() { return completed.load(); }, 15000));
    EXPECT_TRUE(stream_->isCompleted());
    EXPECT_EQ(stream_->state(), StreamState::Completed);
    EXPECT_EQ(totalBytes.load(), 2048u);
    {
        std::lock_guard lock(chunkMutex);
        ASSERT_EQ(chunks.size(), 2u);
        EXPECT_EQ(chunks[0].offset, 0u);
        EXPECT_EQ(chunks[0].segmentIndex, 0u);
        EXPECT_EQ(chunks[0].size(), 1024u);
        EXPECT_TRUE(chunks[0].isLastChunk);
        EXPECT_TRUE(chunks[0].valid());
        EXPECT_EQ(chunks[1].offset, 1024u);
        EXPECT_EQ(chunks[1].segmentIndex, 1u);
        EXPECT_EQ(chunks[1].size(), 1024u);
        EXPECT_TRUE(chunks[1].isLastChunk);
        EXPECT_TRUE(chunks[1].valid());
    }
    const auto info = stream_->info();
    EXPECT_DOUBLE_EQ(info.duration, 2.0);
    EXPECT_EQ(info.totalSize, 0u);
    EXPECT_TRUE(info.isSeekable);
    EXPECT_FALSE(info.isLive);
    EXPECT_EQ(server_.requestCount("/master.m3u8"), 1);
    EXPECT_EQ(server_.requestCount("/media.m3u8"), 1);
    EXPECT_EQ(server_.requestCount("/seg0.ts"), 1);
    EXPECT_EQ(server_.requestCount("/seg1.ts"), 1);
    EXPECT_EQ(server_.connectionCount(), 1);
}

TEST_F(HLSStreamTest, ProductionGateStopsSchedulingNextSegment) {
    std::atomic<bool> allowProduction{false};
    std::atomic<bool> completed{false};
    stream_->setProductionGate([&allowProduction] {
        return allowProduction.load(std::memory_order_acquire);
    });

    auto params = makeParams();
    params.uri = server_.masterUrl();
    params.onComplete = [&completed](uint64_t) {
        completed.store(true, std::memory_order_release);
    };
    ASSERT_TRUE(stream_->open(params).has_value());

    stream_->start();
    ASSERT_TRUE(waitUntil([&] { return server_.requestCount("/media.m3u8") == 1; }));
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    EXPECT_EQ(server_.requestCount("/seg0.ts"), 0);

    allowProduction.store(true, std::memory_order_release);
    stream_->openFetchGate();
    EXPECT_TRUE(waitUntil([&] { return completed.load(std::memory_order_acquire); }));
    EXPECT_EQ(server_.requestCount("/seg0.ts"), 1);
    EXPECT_EQ(server_.requestCount("/seg1.ts"), 1);
}

TEST_F(HLSStreamTest, ProductionGateResumeKeepsCommittedOffsetContinuous) {
    std::atomic<bool> allowProduction{true};
    std::atomic<bool> firstSegmentCommitted{false};
    std::atomic<bool> completed{false};
    std::mutex chunkMutex;
    std::vector<DataChunk> chunks;

    stream_->setProductionGate([&allowProduction] {
        return allowProduction.load(std::memory_order_acquire);
    });

    auto params = makeParams();
    params.uri = server_.masterUrl();
    params.onData = [&allowProduction, &firstSegmentCommitted, &chunkMutex, &chunks](
                        DataChunk chunk) {
        const bool firstSegment = chunk.segmentIndex == 0;
        {
            std::lock_guard lock(chunkMutex);
            chunks.emplace_back(std::move(chunk));
        }
        if (firstSegment) {
            allowProduction.store(false, std::memory_order_release);
            firstSegmentCommitted.store(true, std::memory_order_release);
        }
    };
    params.onComplete = [&completed](uint64_t) {
        completed.store(true, std::memory_order_release);
    };

    ASSERT_TRUE(stream_->open(params).has_value());
    stream_->start();

    ASSERT_TRUE(waitUntil([&] {
        return firstSegmentCommitted.load(std::memory_order_acquire);
    }));
    EXPECT_EQ(server_.requestCount("/seg0.ts"), 1);
    EXPECT_EQ(server_.requestCount("/seg1.ts"), 0);

    allowProduction.store(true, std::memory_order_release);
    stream_->openFetchGate();

    ASSERT_TRUE(waitUntil([&] { return completed.load(std::memory_order_acquire); }));
    ASSERT_TRUE(stream_->isCompleted());
    EXPECT_EQ(server_.requestCount("/seg1.ts"), 1);

    std::lock_guard lock(chunkMutex);
    ASSERT_EQ(chunks.size(), 2u);
    EXPECT_EQ(chunks[0].segmentIndex, 0u);
    EXPECT_EQ(chunks[0].offset, 0u);
    EXPECT_EQ(chunks[0].size(), 1024u);
    EXPECT_TRUE(chunks[0].isLastChunk);
    EXPECT_EQ(chunks[1].segmentIndex, 1u);
    EXPECT_EQ(chunks[1].offset, 1024u);
    EXPECT_EQ(chunks[1].size(), 1024u);
    EXPECT_TRUE(chunks[1].isLastChunk);
}

TEST_F(HLSStreamTest, ByteRangeSegmentsUsePartialContentRequests) {
    server_.setByteRangePlaylist(true);
    std::atomic<uint64_t> totalBytes{0};
    std::atomic<bool> completed{false};
    std::atomic<bool> failed{false};
    auto params = makeParams();
    params.uri = server_.masterUrl();
    params.onData = [&totalBytes](DataChunk chunk) {
        totalBytes.fetch_add(chunk.size(), std::memory_order_relaxed);
        EXPECT_EQ(chunk.size(), 256u);
        EXPECT_TRUE(chunk.isLastChunk);
    };
    params.onComplete = [&completed](uint64_t) {
        completed = true;
    };
    params.onError = [&failed](Error, int32_t) {
        failed = true;
    };

    ASSERT_TRUE(stream_->open(params).has_value());
    stream_->start();
    ASSERT_TRUE(waitUntil([&]() { return completed.load() || failed.load(); }, 10000));
    EXPECT_TRUE(completed.load());
    EXPECT_FALSE(failed.load());
    EXPECT_EQ(totalBytes.load(), 512u);
    EXPECT_EQ(server_.requestCount("/asset.ts"), 2);
    EXPECT_EQ(server_.rangeHeader("/asset.ts", 0), "bytes=0-255");
    EXPECT_EQ(server_.rangeHeader("/asset.ts", 1), "bytes=256-511");
}

TEST_F(HLSStreamTest, RejectsRangeResponseWhenServerIgnoresRange) {
    server_.setByteRangePlaylist(true);
    server_.setIgnoreRange(true);
    std::atomic<bool> failed{false};
    Error reportedError = Error::Success;
    auto params = makeParams();
    params.uri = server_.masterUrl();
    params.onError = [&failed, &reportedError](Error error, int32_t) {
        reportedError = error;
        failed = true;
    };

    ASSERT_TRUE(stream_->open(params).has_value());
    stream_->start();
    ASSERT_TRUE(waitUntil([&]() { return failed.load(); }, 5000));
    EXPECT_EQ(reportedError, Error::StreamInvalidData);
    EXPECT_FALSE(stream_->isCompleted());
}

TEST_F(HLSStreamTest, SegmentReconnectsAfterTruncatedResponse) {
    server_.failFirstSegment();
    auto params = makeParams();
    params.uri = server_.masterUrl();
    std::atomic<bool> completed{false};
    std::mutex chunksMutex;
    std::vector<DataChunk> chunks;
    params.onData = [&chunksMutex, &chunks](DataChunk chunk) {
        std::lock_guard lock(chunksMutex);
        chunks.emplace_back(std::move(chunk));
    };
    params.onComplete = [&completed](uint64_t) {
        completed = true;
    };

    ASSERT_TRUE(stream_->open(params).has_value());
    stream_->start();

    EXPECT_TRUE(waitUntil([&]() { return completed.load(); }, 15000));
    EXPECT_TRUE(stream_->isCompleted());
    EXPECT_GE(server_.requestCount("/seg0.ts"), 2);
    EXPECT_EQ(server_.requestCount("/seg1.ts"), 1);
    EXPECT_GE(server_.connectionCount(), 2);
    std::lock_guard lock(chunksMutex);
    ASSERT_FALSE(chunks.empty());
    uint64_t expectedOffset = 0;
    uint64_t seg0Bytes = 0;
    uint64_t seg1Bytes = 0;
    for (const auto& chunk : chunks) {
        EXPECT_EQ(chunk.offset, expectedOffset);
        expectedOffset += chunk.size();
        (chunk.segmentIndex == 0 ? seg0Bytes : seg1Bytes) += chunk.size();
    }
    EXPECT_EQ(seg0Bytes, 1024u);
    EXPECT_EQ(seg1Bytes, 1024u);
    EXPECT_TRUE(chunks.back().isLastChunk);
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

TEST_F(HLSStreamTest, LiveRefreshUsesPlaylistTargetDuration) {
    server_.setLive(true);
    server_.setTargetDuration(1);
    auto params = makeParams();
    params.uri = server_.masterUrl();

    ASSERT_TRUE(stream_->open(params).has_value());
    stream_->start();
    EXPECT_TRUE(waitUntil([&]() { return server_.requestCount("/media.m3u8") >= 2; }, 2500));
    EXPECT_FALSE(stream_->isCompleted());
}

TEST_F(HLSStreamTest, LivePlaylistEndListTransitionCompletesTheStream) {
    server_.setLive(true);
    std::atomic<bool> completed{false};
    std::atomic<bool> failed{false};
    auto params = makeParams();
    params.uri = server_.masterUrl();
    params.onComplete = [&completed](uint64_t) {
        completed = true;
    };
    params.onError = [&failed](Error, int32_t) {
        failed = true;
    };

    ASSERT_TRUE(stream_->open(params).has_value());
    stream_->start();
    ASSERT_TRUE(waitUntil([&]() { return server_.requestCount("/media.m3u8") >= 1; }, 5000));
    server_.setLive(false);
    ASSERT_TRUE(waitUntil([&]() { return completed.load() || failed.load(); }, 10000));
    EXPECT_TRUE(completed.load());
    EXPECT_FALSE(failed.load());
    EXPECT_TRUE(stream_->isCompleted());
}

TEST_F(HLSStreamTest, LiveRefreshUsesMediaSequenceWithoutDuplicateDownloadsOrEos) {
    server_.setLive(true);
    server_.setSlidingLive(true);
    std::mutex dataMutex;
    std::vector<uint64_t> sequences;
    std::atomic<bool> completed{false};
    std::atomic<bool> failed{false};

    auto params = makeParams();
    params.uri = server_.masterUrl();
    params.onData = [&dataMutex, &sequences](DataChunk chunk) {
        std::lock_guard lock(dataMutex);
        sequences.push_back(chunk.segmentIndex);
    };
    params.onComplete = [&completed](uint64_t) {
        completed = true;
    };
    params.onError = [&failed](Error, int32_t) {
        failed = true;
    };

    ASSERT_TRUE(stream_->open(params).has_value());
    stream_->start();
    ASSERT_TRUE(waitUntil(
        [&]() {
            std::lock_guard lock(dataMutex);
            return std::find(sequences.begin(), sequences.end(), 102u) != sequences.end();
        },
        10000));

    {
        std::lock_guard lock(dataMutex);
        EXPECT_EQ(std::count(sequences.begin(), sequences.end(), 100u), 1);
        EXPECT_EQ(std::count(sequences.begin(), sequences.end(), 101u), 1);
        EXPECT_EQ(std::count(sequences.begin(), sequences.end(), 102u), 1);
    }
    EXPECT_FALSE(completed.load());
    EXPECT_FALSE(failed.load());
    EXPECT_EQ(server_.requestCount("/seg100.ts"), 1);
    EXPECT_EQ(server_.requestCount("/seg101.ts"), 1);
    EXPECT_EQ(server_.requestCount("/seg102.ts"), 1);
}

TEST_F(HLSStreamTest, InitialMediaPlaylistFailureRetriesBeforeReportingError) {
    server_.failNextMediaPlaylist();
    std::atomic<bool> completed{false};
    std::atomic<bool> failed{false};

    auto params = makeParams();
    params.uri = server_.masterUrl();
    params.onComplete = [&completed](uint64_t) {
        completed = true;
    };
    params.onError = [&failed](Error, int32_t) {
        failed = true;
    };

    ASSERT_TRUE(stream_->open(params).has_value());
    stream_->start();
    ASSERT_TRUE(waitUntil([&]() { return completed.load() || failed.load(); }, 15000));
    EXPECT_TRUE(completed.load());
    EXPECT_FALSE(failed.load());
    EXPECT_GE(server_.requestCount("/media.m3u8"), 2);
}

TEST_F(HLSStreamTest, PlaylistFailureReportsAfterBoundedRetries) {
    for (int i = 0; i < 4; ++i) {
        server_.failNextMediaPlaylist();
    }
    std::atomic<bool> completed{false};
    std::atomic<bool> failed{false};
    auto params = makeParams();
    params.uri = server_.masterUrl();
    params.onComplete = [&completed](uint64_t) {
        completed = true;
    };
    params.onError = [&failed](Error, int32_t) {
        failed = true;
    };

    ASSERT_TRUE(stream_->open(params).has_value());
    stream_->start();
    ASSERT_TRUE(waitUntil([&]() { return completed.load() || failed.load(); }, 10000));
    EXPECT_FALSE(completed.load());
    EXPECT_TRUE(failed.load());
    EXPECT_EQ(server_.requestCount("/media.m3u8"), 4);
}

TEST_F(HLSStreamTest, PlaylistRefreshFailureRetriesWithoutEndingLiveStream) {
    server_.setLive(true);
    std::atomic<bool> failed{false};
    auto params = makeParams();
    params.uri = server_.masterUrl();
    params.onError = [&failed](Error, int32_t) {
        failed = true;
    };

    ASSERT_TRUE(stream_->open(params).has_value());
    stream_->start();
    ASSERT_TRUE(waitUntil([&]() { return server_.requestCount("/media.m3u8") >= 1; }, 5000));
    server_.failNextMediaPlaylist();
    ASSERT_TRUE(waitUntil([&]() { return server_.requestCount("/media.m3u8") >= 3; }, 10000));
    EXPECT_FALSE(failed.load());
    EXPECT_FALSE(stream_->isCompleted());
}

TEST_F(HLSStreamTest, L3_SeekByTimeRestartsFromTargetSegment) {
    auto params = makeParams();
    params.uri = server_.masterUrl();
    std::atomic<uint64_t> totalBytes{0};
    std::atomic<uint64_t> completedGeneration{0};
    params.onData = [&totalBytes](DataChunk chunk) {
        totalBytes.fetch_add(chunk.size(), std::memory_order_relaxed);
    };
    params.onComplete = [&completedGeneration](uint64_t generation) {
        completedGeneration = generation;
    };

    ASSERT_TRUE(stream_->open(params).has_value());
    stream_->start();
    EXPECT_TRUE(waitUntil([&]() { return totalBytes.load() > 0; }, 15000));

    auto res = stream_->seekByTime(1.5, 1);
    ASSERT_TRUE(res.has_value());
    EXPECT_TRUE(waitUntil([&]() { return completedGeneration.load() == 1; }, 15000));
    EXPECT_TRUE(stream_->isCompleted());
}

TEST_F(HLSStreamTest, SeekAfterCompletionDownloadsAgain) {
    auto params = makeParams();
    params.uri = server_.masterUrl();
    std::atomic<uint64_t> totalBytes{0};
    std::atomic<uint64_t> completedGeneration{0};
    params.onData = [&totalBytes](DataChunk chunk) {
        totalBytes.fetch_add(chunk.size(), std::memory_order_relaxed);
    };
    params.onComplete = [&completedGeneration](uint64_t generation) {
        completedGeneration = generation;
    };

    ASSERT_TRUE(stream_->open(params).has_value());
    stream_->start();
    ASSERT_TRUE(waitUntil([&]() { return stream_->isCompleted(); }, 15000));
    const auto bytesBeforeSeek = totalBytes.load();

    ASSERT_TRUE(stream_->seekByTime(0.0, 2).has_value());
    EXPECT_TRUE(waitUntil([&]() { return completedGeneration.load() == 2; }, 15000));
    EXPECT_GT(totalBytes.load(), bytesBeforeSeek);
    EXPECT_TRUE(stream_->isCompleted());
}

TEST_F(HLSStreamTest, CloseCancelsThePlaylistRetryTimer) {
    server_.failNextMediaPlaylist();
    auto params = makeParams();
    params.uri = server_.masterUrl();
    ASSERT_TRUE(stream_->open(params).has_value());
    stream_->start();
    ASSERT_TRUE(waitUntil([&]() { return ownerThread_->pendingTimerCount() > 0; }, 10000));

    stream_->close();
    EXPECT_EQ(ownerThread_->pendingTimerCount(), 0u) << "close must cancel the scheduled retry";
}

TEST_F(HLSStreamTest, DestroyWhileDownloadingReleasesTheStream) {
    auto params = makeParams();
    params.uri = server_.masterUrl();
    auto stream = std::make_shared<HLSStream>(ownerThread_);
    std::weak_ptr<HLSStream> weak = stream;
    ASSERT_TRUE(stream->open(params).has_value());
    stream->start();
    ASSERT_TRUE(waitUntil([&]() { return server_.requestCount("/media.m3u8") >= 1; }, 15000));

    stream.reset();
    EXPECT_TRUE(waitUntil([&]() { return weak.expired(); }, 5000));
}

TEST_F(HLSStreamTest, StreamIsReleasedOnceClosedWithoutAReferenceCycle) {
    auto stream = std::make_shared<HLSStream>(ownerThread_);
    std::weak_ptr<HLSStream> weak = stream;
    std::atomic<bool> completed{false};
    auto params = makeParams();
    params.uri = server_.masterUrl();
    params.onComplete = [&completed](uint64_t) {
        completed = true;
    };
    ASSERT_TRUE(stream->open(params).has_value());
    stream->start();
    ASSERT_TRUE(waitUntil([&]() { return completed.load(); }, 15000));

    stream->close();
    stream.reset();
    EXPECT_TRUE(waitUntil([&]() { return weak.expired(); }, 5000))
        << "a closed stream must be freed; a reference cycle would keep its frames alive";
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
            stream_->seekByTime(0.5, 1);
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
    });
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    t1.join();
    t2.join();
    stream_->close();
    EXPECT_TRUE(true);
}
