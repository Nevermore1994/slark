//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <string>
#include <thread>
#include <unistd.h>
#include <vector>

#include "FileStream.h"
#include "MediaSourceManager.h"
#include "FlowConfig.h"
#include "pipeline/FlowConfigDefaults.h"
#include "pipeline/SourceFlowController.h"

using namespace slark;
using namespace slark::pipeline;

namespace {

constexpr size_t kFileSize = 1024 * 1024;
constexpr uint32_t kChunkSize = 64 * 1024;
constexpr size_t kGateLimit = 128 * 1024;

std::string createTempFile(size_t sizeBytes) {
    char tmp[] = "/tmp/slark_gate_XXXXXX";
    const auto fd = mkstemp(tmp);
    if (fd < 0) {
        return {};
    }
    ::close(fd);
    std::string path(tmp);
    std::ofstream out(path, std::ios::binary);
    std::vector<char> data(sizeBytes, 'x');
    out.write(data.data(), static_cast<std::streamsize>(data.size()));
    return path;
}

class CountingFlowDelegate : public ISourceFlowDelegate {
 public:
    void closeFetchGate() noexcept override {
        ++closeCount;
    }
    void openFetchGate() noexcept override {
        ++openCount;
    }
    void interruptStream() noexcept override {
        ++interruptCount;
    }
    void resumeStream() noexcept override {
        ++resumeCount;
    }

    int closeCount = 0;
    int openCount = 0;
    int interruptCount = 0;
    int resumeCount = 0;
};

}  // namespace

// The hysteresis must hold for whatever watermarks the caller configured, so this asserts the
// mechanism against the config's own values instead of hardcoding them. Hardcoding makes the
// test break every time the defaults change, which says nothing about correctness.
TEST(SourceGateTest, LocalWatermarksHonourConfiguredHysteresis) {
    CountingFlowDelegate delegate;
    const auto config = FlowConfigDefaults::forSource(MediaSourceType::Local).source;
    ASSERT_LT(config.lowWatermarkSec, config.highWatermarkSec);

    SourceFlowController flow(config, delegate);

    flow.update(config.highWatermarkSec - 0.1);
    EXPECT_TRUE(flow.isFetchAllowed());

    flow.update(config.highWatermarkSec);
    EXPECT_FALSE(flow.isFetchAllowed());
    EXPECT_EQ(delegate.closeCount, 1);

    const auto midBand = (config.lowWatermarkSec + config.highWatermarkSec) / 2.0;
    flow.update(midBand);
    EXPECT_FALSE(flow.isFetchAllowed()) << "the dead zone must hold the gate closed";

    flow.update(config.lowWatermarkSec);
    EXPECT_TRUE(flow.isFetchAllowed());
    EXPECT_EQ(delegate.openCount, 1);
}

// Sources pull the gate with isFetchAllowed(), so an operation pause has to close it too:
// otherwise a seek could keep the source producing while the stream is paused.
TEST(SourceGateTest, OperationPauseClosesGateUntilResume) {
    CountingFlowDelegate delegate;
    SourceFlowController flow(FlowConfigDefaults::forSource(MediaSourceType::Local).source,
                              delegate);

    flow.pauseForOperation();
    EXPECT_FALSE(flow.isFetchAllowed());
    EXPECT_EQ(delegate.interruptCount, 1);
    EXPECT_EQ(delegate.closeCount, 0) << "the operation path must not use the watermark action";

    flow.resumeForOperation();
    EXPECT_TRUE(flow.isFetchAllowed());
    EXPECT_EQ(delegate.resumeCount, 1);
}

// Regression: a local file used to be read to EOF before the asynchronous pause could react.
// The gate is now pulled at every chunk boundary, so a closed gate bounds the read loop
// instead of letting it drain the whole file.
TEST(SourceGateTest, ClosedGateBoundsFileStreamAtChunkBoundary) {
    const auto path = createTempFile(kFileSize);
    ASSERT_FALSE(path.empty());

    FileStream stream;
    std::atomic<size_t> received{0};
    std::atomic<bool> completed{false};

    OpenParams params;
    params.uri = path;
    params.readChunkSize = kChunkSize;
    params.onData = [&received](DataChunk chunk) {
        received += chunk.size();
    };
    params.onComplete = [&completed](uint64_t) {
        completed = true;
    };
    stream.setProductionGate([&received] {
        return received.load() < kGateLimit;
    });

    ASSERT_TRUE(stream.open(params).has_value());
    stream.start();

    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    stream.close();

    EXPECT_FALSE(completed.load());
    EXPECT_GE(received.load(), kGateLimit);
    EXPECT_LT(received.load(), kFileSize) << "the gate must stop the read loop before EOF";
    std::remove(path.c_str());
}

// Opening the gate has to wake the parked read loop so the file can finish.
TEST(SourceGateTest, OpeningGateResumesFileStream) {
    const auto path = createTempFile(kFileSize);
    ASSERT_FALSE(path.empty());

    FileStream stream;
    std::atomic<size_t> received{0};
    std::atomic<bool> completed{false};
    std::atomic<bool> gateOpen{false};

    OpenParams params;
    params.uri = path;
    params.readChunkSize = kChunkSize;
    params.onData = [&received](DataChunk chunk) {
        received += chunk.size();
    };
    params.onComplete = [&completed](uint64_t) {
        completed = true;
    };
    stream.setProductionGate([&gateOpen] {
        return gateOpen.load();
    });

    ASSERT_TRUE(stream.open(params).has_value());
    stream.start();

    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    EXPECT_EQ(received.load(), 0u) << "a closed gate must stop production before the first chunk";

    gateOpen.store(true);
    stream.openFetchGate();

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (!completed.load() && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    stream.close();

    EXPECT_TRUE(completed.load());
    EXPECT_EQ(received.load(), kFileSize);
    std::remove(path.c_str());
}

// MediaSourceManager used to dispatch by dynamic_cast<HLSStream*>: a file source went
// through the destructive pause() path, which no gate ever reached. It must now forward
// to the stream's gate interface regardless of type.
TEST(SourceGateTest, MediaSourceManagerForwardsGateForFileSource) {
    const auto path = createTempFile(kFileSize);
    ASSERT_FALSE(path.empty());

    MediaSourceManager manager;
    std::atomic<size_t> received{0};
    ASSERT_TRUE(manager.open(
        path,
        [&received](DataPacket packet) {
            received += packet.data ? packet.data->size() : 0;
        },
        [](Error, int32_t) {},
        [] {}));

    manager.setProductionGate([] {
        return false;
    });
    manager.start();
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    manager.close();

    EXPECT_EQ(received.load(), 0u) << "the gate must reach the file stream through the manager";
    std::remove(path.c_str());
}
