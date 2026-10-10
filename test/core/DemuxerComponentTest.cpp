//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#include <gtest/gtest.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <future>
#include <fstream>
#include <memory>
#include <mutex>
#include <string_view>
#include <thread>
#include <vector>

#include "Data.h"
#include "DemuxerComponent.h"

using namespace slark;

namespace {

constexpr const char* kTsPath = "../../test_sample/segment-0.ts";
constexpr const char* kMp4Path = "../../test_sample/Sample.mp4";

DataPtr loadData(std::string_view path, uint64_t offset, uint64_t size) {
    std::ifstream input(std::string(path), std::ios::binary | std::ios::ate);
    if (!input) {
        return nullptr;
    }
    const auto fileSize = static_cast<uint64_t>(input.tellg());
    if (offset >= fileSize) {
        return nullptr;
    }
    size = std::min(size, fileSize - offset);
    input.seekg(static_cast<std::streamoff>(offset), std::ios::beg);
    auto data = Data::create(size);
    if (!data || !data->data()) {
        return nullptr;
    }
    input.read(reinterpret_cast<char*>(data->data()), static_cast<std::streamsize>(size));
    if (static_cast<uint64_t>(input.gcount()) != size) {
        return nullptr;
    }
    data->setSize(size);
    return data;
}

DataPtr loadTsData() {
    std::ifstream input(kTsPath, std::ios::binary | std::ios::ate);
    if (!input) {
        return nullptr;
    }

    const auto size = static_cast<uint64_t>(input.tellg());
    input.seekg(0, std::ios::beg);
    auto data = Data::create(size);
    if (!data || !data->data()) {
        return nullptr;
    }
    input.read(reinterpret_cast<char*>(data->data()), static_cast<std::streamsize>(size));
    data->setSize(size);
    return data;
}

struct ResultState {
    std::mutex mutex;
    std::condition_variable condition;
    size_t videoFrames = 0;
    size_t audioFrames = 0;
    size_t parsedHeaders = 0;
    size_t fileEnds = 0;
    uint64_t headerSampleRate = 0;
    uint16_t headerChannels = 0;
    bool demuxerValid = true;
    std::vector<DemuxerResultCode> resultCodes;
    std::vector<bool> resultHasFrames;
};

class BlockingDemuxer final : public IDemuxer {
 public:
    BlockingDemuxer(std::promise<void>& parseEntered, std::shared_future<void> continueParse)
        : parseEntered_(parseEntered), continueParse_(std::move(continueParse)) {
        type_ = DemuxerType::MP4;
        isOpened_ = true;
    }

    bool open(std::unique_ptr<Buffer>&) noexcept override {
        return true;
    }

    void close() noexcept override {}

    DemuxerResult parseData(DataPacket&) noexcept override {
        parseThread_ = std::this_thread::get_id();
        parsing_.store(true, std::memory_order_release);
        parseEntered_.set_value();
        continueParse_.wait();
        parsing_.store(false, std::memory_order_release);
        return {};
    }

    uint64_t getSeekToPos(double) noexcept override {
        return 0;
    }

    void flush() noexcept override {
        flushThread_ = std::this_thread::get_id();
        if (parsing_.load(std::memory_order_acquire)) {
            concurrentFlushes_.fetch_add(1, std::memory_order_relaxed);
        }
        flushCount_.fetch_add(1, std::memory_order_relaxed);
    }

    [[nodiscard]] int flushCount() const noexcept {
        return flushCount_.load(std::memory_order_relaxed);
    }

    [[nodiscard]] int concurrentFlushes() const noexcept {
        return concurrentFlushes_.load(std::memory_order_relaxed);
    }

    [[nodiscard]] std::thread::id parseThread() const noexcept {
        return parseThread_;
    }

    [[nodiscard]] std::thread::id flushThread() const noexcept {
        return flushThread_;
    }

 private:
    std::promise<void>& parseEntered_;
    std::shared_future<void> continueParse_;
    std::atomic<bool> parsing_{false};
    std::thread::id parseThread_;
    std::thread::id flushThread_;
    std::atomic<int> flushCount_{0};
    std::atomic<int> concurrentFlushes_{0};
};

}  // namespace

TEST(DemuxerComponentTest, FlushWaitsForWorkerParseAndRunsWithoutConcurrentDemuxerAccess) {
    auto [sender, receiver] = Channel<DataPacket>::create();
    DemuxerConfig config;
    auto component = std::make_shared<DemuxerComponent>(std::move(receiver), std::move(config));

    std::promise<void> parseEntered;
    auto parseEnteredFuture = parseEntered.get_future();
    std::promise<void> continueParse;
    auto demuxer = std::make_shared<BlockingDemuxer>(parseEntered, continueParse.get_future().share());
    component->setDemuxer(demuxer);
    component->start();

    DataPacket packet;
    packet.offset = 0;
    packet.data = Data::create(1);
    ASSERT_TRUE(sender->send(std::move(packet)).has_value());
    if (parseEnteredFuture.wait_for(std::chrono::seconds(2)) != std::future_status::ready) {
        continueParse.set_value();
        component->stop();
        FAIL() << "worker did not enter parseData";
        return;
    }

    component->pause();
    auto flushReturned = std::make_shared<std::atomic<bool>>(false);
    std::thread flushThread([component, flushReturned] {
        component->flush();
        flushReturned->store(true, std::memory_order_release);
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    const bool returnedBeforeParseCompleted = flushReturned->load(std::memory_order_acquire);
    continueParse.set_value();

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (!flushReturned->load(std::memory_order_acquire) &&
           std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    if (flushReturned->load(std::memory_order_acquire)) {
        flushThread.join();
        EXPECT_FALSE(returnedBeforeParseCompleted);
        EXPECT_EQ(demuxer->flushCount(), 1);
        EXPECT_EQ(demuxer->concurrentFlushes(), 0);
        EXPECT_EQ(demuxer->flushThread(), demuxer->parseThread());
        component->resume();
        component->stop();
    } else {
        flushThread.detach();
        ADD_FAILURE() << "flush did not complete after the worker finished parseData";
    }
}

TEST(DemuxerComponentTest, SourceOperationGenerationIsMonotonic) {
    auto [sender, receiver] = Channel<DataPacket>::create();
    DemuxerConfig config;
    DemuxerComponent component(std::move(receiver), std::move(config));

    const auto first = component.beginSourceOperation();
    const auto second = component.beginSourceOperation();

    EXPECT_EQ(first, 1u);
    EXPECT_EQ(second, 2u);
    EXPECT_TRUE(component.isCurrentSourceGeneration(second));
    EXPECT_FALSE(component.isCurrentSourceGeneration(first));
}

TEST(DemuxerComponentTest, InitialSeekDiscardsQueuedPacketsAndEos) {
    auto firstData = loadData(kMp4Path, 0, 65536);
    if (!firstData) {
        GTEST_SKIP() << "Sample.mp4 not found, skipping";
    }

    auto [sender, receiver] = Channel<DataPacket>::create();
    DemuxerConfig config;
    config.filePath = kMp4Path;
    DemuxerComponent component(std::move(receiver), std::move(config));
    ResultState state;

    component.setHandleResultFunc(
        [&state](const std::shared_ptr<IDemuxer>& demuxer, DemuxerResult&& result) {
            std::lock_guard lock(state.mutex);
            state.resultCodes.emplace_back(result.resultCode);
            state.resultHasFrames.emplace_back(!result.audioFrames.empty() ||
                                               !result.videoFrames.empty());
            if (result.resultCode == DemuxerResultCode::ParsedHeader) {
                ++state.parsedHeaders;
            }
            if (result.resultCode == DemuxerResultCode::FileEnd) {
                ++state.fileEnds;
            }
            state.videoFrames += result.videoFrames.size();
            state.audioFrames += result.audioFrames.size();
            state.demuxerValid = state.demuxerValid && demuxer != nullptr;
            state.condition.notify_all();
        });

    auto* senderPtr = sender.get();
    component.setHandleSeekFunc([senderPtr](Range range, uint64_t generation) {
        DataPacket packet;
        packet.sourceGeneration = generation;
        packet.offset = range.start();
        packet.data = loadData(kMp4Path, range.start(), 65536);
        if (packet.data) {
            senderPtr->post(std::move(packet));
        }
        DataPacket eos;
        eos.isEndOfStream = true;
        eos.sourceGeneration = generation;
        senderPtr->post(std::move(eos));
    });

    DataPacket firstPacket;
    firstPacket.offset = 0;
    firstPacket.data = std::move(firstData);
    ASSERT_TRUE(sender->send(std::move(firstPacket)).has_value());
    for (const auto offset : {uint64_t{65536}, uint64_t{131072}}) {
        DataPacket stalePacket;
        stalePacket.offset = offset;
        stalePacket.data = loadData(kMp4Path, offset, 65536);
        ASSERT_NE(stalePacket.data, nullptr);
        ASSERT_TRUE(sender->send(std::move(stalePacket)).has_value());
    }
    DataPacket staleEos;
    staleEos.isEndOfStream = true;
    ASSERT_TRUE(sender->send(std::move(staleEos)).has_value());

    component.start();
    {
        std::unique_lock lock(state.mutex);
        ASSERT_TRUE(state.condition.wait_for(lock, std::chrono::seconds(10), [&state] {
            return state.fileEnds == 1 && state.audioFrames > 0 && state.videoFrames > 0;
        }));
        ASSERT_TRUE(state.demuxerValid);
        auto firstFrame = std::find(state.resultHasFrames.begin(), state.resultHasFrames.end(), true);
        auto firstFileEnd = std::find(state.resultCodes.begin(), state.resultCodes.end(),
                                       DemuxerResultCode::FileEnd);
        ASSERT_NE(firstFrame, state.resultHasFrames.end());
        ASSERT_NE(firstFileEnd, state.resultCodes.end());
        EXPECT_EQ(std::count(state.resultCodes.begin(), state.resultCodes.end(),
                             DemuxerResultCode::InvalidData), 0u);
        EXPECT_LT(static_cast<size_t>(firstFrame - state.resultHasFrames.begin()),
                  static_cast<size_t>(firstFileEnd - state.resultCodes.begin()));
    }
    component.stop();
}

TEST(DemuxerComponentTest, SeekAfterFlushPreservesFirstSeekPacket) {
    auto firstData = loadData(kMp4Path, 0, 65536);
    if (!firstData) {
        GTEST_SKIP() << "Sample.mp4 not found, skipping";
    }

    auto [sender, receiver] = Channel<DataPacket>::create();
    DemuxerConfig config;
    config.filePath = kMp4Path;
    DemuxerComponent component(std::move(receiver), std::move(config));
    ResultState state;

    component.setHandleResultFunc(
        [&state](const std::shared_ptr<IDemuxer>& demuxer, DemuxerResult&& result) {
            std::lock_guard lock(state.mutex);
            state.resultCodes.emplace_back(result.resultCode);
            state.resultHasFrames.emplace_back(!result.audioFrames.empty() ||
                                               !result.videoFrames.empty());
            if (result.resultCode == DemuxerResultCode::ParsedHeader) {
                ++state.parsedHeaders;
            }
            if (result.resultCode == DemuxerResultCode::FileEnd) {
                ++state.fileEnds;
            }
            state.videoFrames += result.videoFrames.size();
            state.audioFrames += result.audioFrames.size();
            state.demuxerValid = state.demuxerValid && demuxer != nullptr;
            state.condition.notify_all();
        });

    auto* senderPtr = sender.get();
    component.setHandleSeekFunc([senderPtr](Range range, uint64_t generation) {
        DataPacket packet;
        packet.sourceGeneration = generation;
        packet.offset = range.start();
        packet.data = loadData(kMp4Path, range.start(), 65536);
        if (packet.data) {
            senderPtr->post(std::move(packet));
        }
        DataPacket eos;
        eos.isEndOfStream = true;
        eos.sourceGeneration = generation;
        senderPtr->post(std::move(eos));
    });

    component.start();
    DataPacket firstPacket;
    firstPacket.offset = 0;
    firstPacket.data = std::move(firstData);
    ASSERT_TRUE(sender->send(std::move(firstPacket)).has_value());

    {
        std::unique_lock lock(state.mutex);
        ASSERT_TRUE(state.condition.wait_for(lock, std::chrono::seconds(10), [&state] {
            return state.fileEnds == 1 && state.videoFrames > 0;
        }));
    }

    const auto seekPos = component.getSeekToPos(1.0);
    ASSERT_GT(seekPos, 0u);
    size_t framesBeforeSeek = 0;
    {
        std::lock_guard lock(state.mutex);
        framesBeforeSeek = state.videoFrames + state.audioFrames;
    }

    component.pause();
    component.flush();
    component.seekToPos(seekPos);
    component.resume();

    {
        std::unique_lock lock(state.mutex);
        ASSERT_TRUE(state.condition.wait_for(lock, std::chrono::seconds(10), [&state, framesBeforeSeek] {
            return state.fileEnds >= 2 && state.videoFrames + state.audioFrames > framesBeforeSeek;
        }));
        EXPECT_TRUE(state.demuxerValid);
    }
    component.stop();
}

TEST(DemuxerComponentTest, PreferredTsDemuxerReceivesDataAndOrderedEos) {
    auto data = loadTsData();
    if (!data) {
        GTEST_SKIP() << "segment-0.ts not found, skipping";
    }

    auto [sender, receiver] = Channel<DataPacket>::create();
    DemuxerConfig config;
    config.filePath = kTsPath;
    config.preferredType = DemuxerType::TS;
    DemuxerComponent component(std::move(receiver), std::move(config));
    ResultState state;

    component.setHandleResultFunc(
        [&state](const std::shared_ptr<IDemuxer>& demuxer, DemuxerResult&& result) {
            std::lock_guard lock(state.mutex);
            state.resultCodes.emplace_back(result.resultCode);
            state.resultHasFrames.emplace_back(!result.audioFrames.empty() ||
                                               !result.videoFrames.empty());
            if (result.resultCode == DemuxerResultCode::ParsedHeader) {
                ++state.parsedHeaders;
                if (const auto info = demuxer->audioInfo()) {
                    state.headerSampleRate = info->sampleRate;
                    state.headerChannels = info->channels;
                }
            }
            if (result.resultCode == DemuxerResultCode::FileEnd) {
                ++state.fileEnds;
            }
            state.videoFrames += result.videoFrames.size();
            state.audioFrames += result.audioFrames.size();
            state.demuxerValid = state.demuxerValid && demuxer != nullptr;
            state.condition.notify_all();
        });

    component.start();
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    {
        std::lock_guard lock(state.mutex);
        EXPECT_EQ(state.parsedHeaders, 0u);
    }

    DataPacket packet;
    packet.data = std::move(data);
    packet.offset = 0;
    packet.segmentIndex = 4;
    ASSERT_TRUE(sender->send(std::move(packet)).has_value());

    DataPacket duplicatePacket;
    duplicatePacket.data = Data::fromString("duplicate");
    duplicatePacket.offset = 0;
    duplicatePacket.segmentIndex = 4;
    ASSERT_TRUE(sender->send(std::move(duplicatePacket)).has_value());

    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    DataPacket eos;
    eos.isEndOfStream = true;
    ASSERT_TRUE(sender->send(std::move(eos)).has_value());

    {
        std::unique_lock lock(state.mutex);
        ASSERT_TRUE(state.condition.wait_for(lock, std::chrono::seconds(10), [&state] {
            return state.fileEnds == 1;
        }));
        ASSERT_GE(state.parsedHeaders, 1u);
        EXPECT_GT(state.headerSampleRate, 0u);
        EXPECT_GT(state.headerChannels, 0u);
        EXPECT_GT(state.videoFrames, 0u);
        EXPECT_GT(state.audioFrames, 0u);
        EXPECT_TRUE(state.demuxerValid);
        ASSERT_FALSE(state.resultCodes.empty());
        EXPECT_EQ(state.resultCodes.back(), DemuxerResultCode::FileEnd);
        EXPECT_TRUE(std::all_of(state.resultCodes.begin(), state.resultCodes.end() - 1,
                                 [](auto code) { return code != DemuxerResultCode::FileEnd; }));
    }

    EXPECT_EQ(component.type(), DemuxerType::TS);
    EXPECT_TRUE(component.isOpen());
    EXPECT_TRUE(component.isCompleted());
    component.stop();
}

TEST(DemuxerComponentTest, SeekDataWaitsForResumeAfterOperationPause) {
    auto initialData = loadTsData();
    if (!initialData) {
        GTEST_SKIP() << "segment-0.ts not found, skipping";
    }

    auto [sender, receiver] = Channel<DataPacket>::create();
    DemuxerConfig config;
    config.filePath = kTsPath;
    config.preferredType = DemuxerType::TS;
    DemuxerComponent component(std::move(receiver), std::move(config));
    ResultState state;

    component.setHandleResultFunc(
        [&state](const std::shared_ptr<IDemuxer>& demuxer, DemuxerResult&& result) {
            std::lock_guard lock(state.mutex);
            state.resultCodes.emplace_back(result.resultCode);
            state.videoFrames += result.videoFrames.size();
            state.audioFrames += result.audioFrames.size();
            state.demuxerValid = state.demuxerValid && demuxer != nullptr;
            state.condition.notify_all();
        });

    component.start();
    DataPacket initialPacket;
    initialPacket.data = std::move(initialData);
    initialPacket.offset = 0;
    initialPacket.segmentIndex = 0;
    ASSERT_TRUE(sender->send(std::move(initialPacket)).has_value());

    {
        std::unique_lock lock(state.mutex);
        ASSERT_TRUE(state.condition.wait_for(lock, std::chrono::seconds(10), [&state] {
            return state.videoFrames > 0 && state.audioFrames > 0;
        }));
    }

    size_t framesBeforeSeek = 0;
    {
        std::lock_guard lock(state.mutex);
        framesBeforeSeek = state.videoFrames + state.audioFrames;
    }

    component.pause();
    component.flush();
    const auto generation = component.beginSourceOperation();
    auto seekData = loadTsData();
    ASSERT_NE(seekData, nullptr);
    DataPacket seekPacket;
    seekPacket.data = std::move(seekData);
    seekPacket.offset = 0;
    seekPacket.segmentIndex = 1;
    seekPacket.sourceGeneration = generation;
    ASSERT_TRUE(sender->send(std::move(seekPacket)).has_value());

    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    {
        std::lock_guard lock(state.mutex);
        EXPECT_EQ(state.videoFrames + state.audioFrames, framesBeforeSeek);
    }

    component.resume();
    {
        std::unique_lock lock(state.mutex);
        ASSERT_TRUE(state.condition.wait_for(lock, std::chrono::seconds(10), [&state, framesBeforeSeek] {
            return state.videoFrames + state.audioFrames > framesBeforeSeek;
        }));
        EXPECT_TRUE(state.demuxerValid);
    }
    component.stop();
}
