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
#include <latch>
#include <memory>
#include <mutex>
#include <set>
#include <string_view>
#include <sys/resource.h>
#include <thread>
#include <vector>

#include "Data.h"
#include "DemuxerComponent.h"

using namespace slark;
using namespace std::chrono_literals;

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

// Counts parseData/flush/close/getSeekToPos with calling threads and a shared
// overlap detector: no two of these methods may ever run concurrently.
class CountingDemuxer final : public IDemuxer {
 public:
    CountingDemuxer() { isOpened_ = true; }

    bool open(std::unique_ptr<Buffer>&) noexcept override {
        return true;
    }

    void close() noexcept override {
        enter();
        ++closeCount_;
        closeThread_ = std::this_thread::get_id();
        exit();
    }

    DemuxerResult parseData(DataPacket&) noexcept override {
        enter();
        ++parseCount_;
        parseThread_ = std::this_thread::get_id();
        if (blockParse_) {
            parseEntered_->set_value();
            continueParse_.wait();
        }
        if (publishInfoInParse_) {
            auto video = std::make_shared<VideoInfo>();
            video->width = 1920;
            video->height = 1080;
            video->fps = 30;
            videoInfo_ = video;
            auto audio = std::make_shared<AudioInfo>();
            audio->sampleRate = 48000;
            audioInfo_ = audio;
            totalDuration_ = CTime(12.5);
            inPlaceVideo_ = video;  // kept for Q3 in-place mutation checks
        }
        DemuxerResult result;
        if (publishInfoInParse_) {
            result.resultCode = DemuxerResultCode::ParsedHeader;
        }
        exit();
        return result;
    }

    void seekPos(uint64_t) noexcept override {
        enter();
        ++seekPosCount_;
        seekThread_ = std::this_thread::get_id();
        exit();
    }

    uint64_t getSeekToPos(double) noexcept override {
        enter();
        ++getSeekToPosCount_;
        getSeekToPosThread_ = std::this_thread::get_id();
        exit();
        return 1024;
    }

    void flush() noexcept override {
        enter();
        ++flushCount_;
        exit();
    }

    void enableBlockingParse(std::promise<void>& entered, std::shared_future<void> cont) {
        blockParse_ = true;
        parseEntered_ = &entered;
        continueParse_ = std::move(cont);
    }

    void setPublishInfoInParse(bool enabled) noexcept { publishInfoInParse_ = enabled; }

    void mutateVideoInfoInPlace() noexcept {
        if (inPlaceVideo_) {
            inPlaceVideo_->width = 1;
            inPlaceVideo_->fps = 1;
        }
    }

    [[nodiscard]] int closeCount() const noexcept { return closeCount_.load(); }
    [[nodiscard]] int parseCount() const noexcept { return parseCount_.load(); }
    [[nodiscard]] int flushCount() const noexcept { return flushCount_.load(); }
    [[nodiscard]] int seekPosCount() const noexcept { return seekPosCount_.load(); }
    [[nodiscard]] int getSeekToPosCount() const noexcept { return getSeekToPosCount_.load(); }
    [[nodiscard]] int overlaps() const noexcept { return overlaps_.load(); }
    [[nodiscard]] std::thread::id parseThread() const noexcept { return parseThread_; }
    [[nodiscard]] std::thread::id seekThread() const noexcept { return seekThread_; }
    [[nodiscard]] std::thread::id getSeekToPosThread() const noexcept {
        return getSeekToPosThread_;
    }
    [[nodiscard]] std::thread::id closeThread() const noexcept { return closeThread_; }

 private:
    void enter() noexcept {
        if (active_.fetch_add(1, std::memory_order_acq_rel) != 0) {
            overlaps_.fetch_add(1, std::memory_order_relaxed);
        }
    }

    void exit() noexcept { active_.fetch_sub(1, std::memory_order_acq_rel); }

    std::atomic<int> active_{0};
    std::atomic<int> overlaps_{0};
    std::atomic<int> closeCount_{0};
    std::atomic<int> parseCount_{0};
    std::atomic<int> flushCount_{0};
    std::atomic<int> seekPosCount_{0};
    std::atomic<int> getSeekToPosCount_{0};
    std::atomic<bool> blockParse_{false};
    std::atomic<bool> publishInfoInParse_{false};
    std::promise<void>* parseEntered_ = nullptr;
    std::shared_future<void> continueParse_;
    std::thread::id parseThread_;
    std::thread::id seekThread_;
    std::thread::id getSeekToPosThread_;
    std::thread::id closeThread_;
    std::shared_ptr<VideoInfo> inPlaceVideo_;
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

    EXPECT_TRUE(state.demuxerValid);
    EXPECT_TRUE(state.parsedHeaders >= 1);
    EXPECT_TRUE(state.fileEnds == 1);
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

namespace {

void sendPacket(const SenderPtr<DataPacket>& sender, uint64_t offset) {
    DataPacket packet;
    packet.data = Data::create(16);
    packet.offset = offset;
    packet.sourceGeneration = 1;
    ASSERT_TRUE(sender->send(std::move(packet)).has_value());
}

}  // namespace

TEST(DemuxerComponentTest, PausedWorkerDoesNotParseUntilResume) {
    auto [sender, receiver] = Channel<DataPacket>::create();
    DemuxerConfig config;
    DemuxerComponent component(std::move(receiver), std::move(config));
    auto demuxer = std::make_shared<CountingDemuxer>();
    component.setDemuxer(demuxer);
    component.start();

    sendPacket(sender, 0);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (demuxer->parseCount() == 0 && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::yield();
    }
    ASSERT_GE(demuxer->parseCount(), 1);

    component.pause();
    component.pause();  // idempotent
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    const auto parsesAtPause = demuxer->parseCount();
    sendPacket(sender, 1);
    sendPacket(sender, 2);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    EXPECT_EQ(demuxer->parseCount(), parsesAtPause);

    component.resume();
    const auto resumeDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (demuxer->parseCount() < parsesAtPause + 2 &&
           std::chrono::steady_clock::now() < resumeDeadline) {
        std::this_thread::yield();
    }
    EXPECT_GE(demuxer->parseCount(), parsesAtPause + 2);
    EXPECT_EQ(demuxer->overlaps(), 0);
    component.stop();
}

TEST(DemuxerComponentTest, PauseFlushThenResumeParsesFirstPacketAfterResume) {
    auto [sender, receiver] = Channel<DataPacket>::create();
    DemuxerConfig config;
    DemuxerComponent component(std::move(receiver), std::move(config));
    auto demuxer = std::make_shared<CountingDemuxer>();
    component.setDemuxer(demuxer);
    component.start();
    sendPacket(sender, 0);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (demuxer->parseCount() == 0 && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::yield();
    }

    component.pause();
    component.flush();
    sendPacket(sender, 1);
    std::this_thread::sleep_for(std::chrono::milliseconds(80));
    EXPECT_EQ(demuxer->parseCount(), 1);

    component.resume();
    const auto resumeDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (demuxer->parseCount() < 2 && std::chrono::steady_clock::now() < resumeDeadline) {
        std::this_thread::yield();
    }
    EXPECT_GE(demuxer->parseCount(), 2);
    EXPECT_EQ(demuxer->overlaps(), 0);
    component.stop();
}

TEST(DemuxerComponentTest, SeekTasksRunOnWorkerWhilePaused) {
    auto [sender, receiver] = Channel<DataPacket>::create();
    DemuxerConfig config;
    DemuxerComponent component(std::move(receiver), std::move(config));
    auto demuxer = std::make_shared<CountingDemuxer>();
    component.setDemuxer(demuxer);
    component.start();
    // One parse first so parseThread_ records the worker id for the equality check.
    sendPacket(sender, 0);
    const auto parseDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (demuxer->parseCount() == 0 && std::chrono::steady_clock::now() < parseDeadline) {
        std::this_thread::yield();
    }
    ASSERT_GE(demuxer->parseCount(), 1);
    component.pause();

    component.seekToPos(4096);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (demuxer->seekPosCount() == 0 && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::yield();
    }
    EXPECT_GE(demuxer->seekPosCount(), 1);
    EXPECT_EQ(demuxer->seekThread(), demuxer->parseThread()) << "seek must run on the worker";
    EXPECT_EQ(demuxer->overlaps(), 0);
    component.stop();
}

TEST(DemuxerComponentTest, FlushAndStopConvergeWithoutHang) {
    for (int round = 0; round < 200; ++round) {
        auto [sender, receiver] = Channel<DataPacket>::create();
        DemuxerConfig config;
        auto component =
            std::make_shared<DemuxerComponent>(std::move(receiver), std::move(config));
        auto demuxer = std::make_shared<CountingDemuxer>();
        component->setDemuxer(demuxer);
        component->start();
        sendPacket(sender, round);

        auto flushDone = std::async(std::launch::async, [&] { component->flush(); });
        component->stop();
        EXPECT_EQ(flushDone.wait_for(std::chrono::seconds(1)), std::future_status::ready)
            << "round " << round;
        EXPECT_EQ(demuxer->closeCount(), 1) << "round " << round;
        EXPECT_EQ(demuxer->overlaps(), 0) << "round " << round;
    }
}

TEST(DemuxerComponentTest, ConcurrentStopFinalizesExactlyOnce) {
    auto [sender, receiver] = Channel<DataPacket>::create();
    DemuxerConfig config;
    DemuxerComponent component(std::move(receiver), std::move(config));
    auto demuxer = std::make_shared<CountingDemuxer>();
    component.setDemuxer(demuxer);
    component.start();
    sendPacket(sender, 0);

    constexpr int kStoppers = 16;
    std::latch ready{kStoppers};
    std::vector<std::thread> stoppers;
    for (int i = 0; i < kStoppers; ++i) {
        stoppers.emplace_back([&] {
            ready.count_down();
            ready.wait();
            component.stop();
        });
    }
    for (auto& stopper : stoppers) {
        stopper.join();
    }
    EXPECT_EQ(demuxer->closeCount(), 1);
    EXPECT_EQ(demuxer->overlaps(), 0);
}

TEST(DemuxerComponentTest, StopFromResultCallbackDefersFinalize) {
    auto [sender, receiver] = Channel<DataPacket>::create();
    DemuxerConfig config;
    DemuxerComponent component(std::move(receiver), std::move(config));
    auto demuxer = std::make_shared<CountingDemuxer>();
    component.setDemuxer(demuxer);

    std::promise<void> callbackRan;
    auto callbackFuture = callbackRan.get_future();
    component.setHandleResultFunc([&component, &callbackRan](const std::shared_ptr<IDemuxer>&,
                                                             DemuxerResult&&) {
        component.stop();  // called on the worker itself: request-only
        callbackRan.set_value();
    });

    component.start();
    sendPacket(sender, 0);
    ASSERT_EQ(callbackFuture.wait_for(5s), std::future_status::ready);
    EXPECT_EQ(demuxer->closeCount(), 0) << "finalize must be deferred to an external stop";

    component.stop();
    EXPECT_EQ(demuxer->closeCount(), 1);
}

TEST(DemuxerComponentTest, FlushDuringStopWindowIsSkippedWithoutCallingDemuxer) {
    auto [sender, receiver] = Channel<DataPacket>::create();
    DemuxerConfig config;
    DemuxerComponent component(std::move(receiver), std::move(config));
    auto demuxer = std::make_shared<CountingDemuxer>();
    component.setDemuxer(demuxer);
    component.start();

    std::promise<void> parseEntered;
    std::promise<void> continueParse;
    demuxer->enableBlockingParse(parseEntered, continueParse.get_future().share());
    sendPacket(sender, 0);
    ASSERT_EQ(parseEntered.get_future().wait_for(2s), std::future_status::ready);

    std::thread stopper([&] { component.stop(); });
    // Give the stopper time to set stopRequested_ while the worker stays pinned in
    // parseData; flush must then skip instead of touching IDemuxer.
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    component.flush();
    EXPECT_EQ(demuxer->flushCount(), 0);

    continueParse.set_value();
    stopper.join();
    EXPECT_EQ(demuxer->closeCount(), 1);
    EXPECT_EQ(demuxer->overlaps(), 0);
}

TEST(DemuxerComponentTest, FlushBeforeStartInlinesAndAfterWorkerExitSkips) {
    auto [sender, receiver] = Channel<DataPacket>::create();
    DemuxerConfig config;
    DemuxerComponent component(std::move(receiver), std::move(config));
    auto demuxer = std::make_shared<CountingDemuxer>();
    component.setDemuxer(demuxer);

    component.flush();  // no worker yet: inline on the caller thread
    EXPECT_EQ(demuxer->flushCount(), 1);

    component.start();
    sender.reset();  // closing the sender ends the loop naturally
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (component.isRunning() && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::yield();
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    EXPECT_FALSE(component.isRunning()) << "worker should have exited after channel close";

    const auto flushesBefore = demuxer->flushCount();
    component.flush();  // exited worker: skipped, no crash, no extra flush
    EXPECT_EQ(demuxer->flushCount(), flushesBefore);
    component.stop();
    EXPECT_EQ(demuxer->closeCount(), 1);
}

TEST(DemuxerComponentTest, GetSeekToPosRunsOnWorkerAndNeverOverlaps) {
    auto [sender, receiver] = Channel<DataPacket>::create();
    DemuxerConfig config;
    DemuxerComponent component(std::move(receiver), std::move(config));
    auto demuxer = std::make_shared<CountingDemuxer>();
    component.setDemuxer(demuxer);

    EXPECT_EQ(component.getSeekToPos(1.0), 1024u);  // no worker: inline, demuxer is opened
    EXPECT_EQ(demuxer->getSeekToPosCount(), 1);

    component.start();
    sendPacket(sender, 0);
    const auto parseDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (demuxer->parseCount() == 0 && std::chrono::steady_clock::now() < parseDeadline) {
        std::this_thread::yield();
    }
    ASSERT_GE(demuxer->parseCount(), 1);
    EXPECT_EQ(component.getSeekToPos(1.0), 1024u);
    EXPECT_EQ(demuxer->getSeekToPosThread(), demuxer->parseThread());

    component.stop();
    EXPECT_EQ(component.getSeekToPos(1.0), 0u);  // stopped: skipped
    EXPECT_EQ(demuxer->getSeekToPosCount(), 2);

    constexpr int kCallers = 8;
    std::vector<std::thread> callers;
    for (int i = 0; i < kCallers; ++i) {
        callers.emplace_back([&] { (void)component.getSeekToPos(1.0); });
    }
    for (auto& caller : callers) {
        caller.join();
    }
    EXPECT_EQ(demuxer->overlaps(), 0);
}

TEST(DemuxerComponentTest, HeaderSnapshotPublishedBeforeCallbackAndClearedOnStop) {
    auto [sender, receiver] = Channel<DataPacket>::create();
    DemuxerConfig config;
    DemuxerComponent component(std::move(receiver), std::move(config));
    auto demuxer = std::make_shared<CountingDemuxer>();
    demuxer->setPublishInfoInParse(true);
    component.setDemuxer(demuxer);

    EXPECT_FALSE(component.hasVideo());
    EXPECT_FALSE(component.hasAudio());
    EXPECT_EQ(component.videoInfo(), nullptr);

    std::promise<void> callbackRan;
    auto callbackFuture = callbackRan.get_future();
    std::shared_ptr<VideoInfo> seenInCallback;
    component.setHandleResultFunc([&](const std::shared_ptr<IDemuxer>&, DemuxerResult&&) {
        EXPECT_TRUE(component.hasVideo());
        EXPECT_TRUE(component.hasAudio());
        auto video = component.videoInfo();
        ASSERT_NE(video, nullptr);
        EXPECT_EQ(video->width, 1920);
        EXPECT_EQ(video->fps, 30);
        EXPECT_DOUBLE_EQ(component.totalDuration().second(), 12.5);
        seenInCallback = video;
        callbackRan.set_value();
    });

    component.start();
    sendPacket(sender, 0);
    ASSERT_EQ(callbackFuture.wait_for(5s), std::future_status::ready);

    // Q3: in-place mutation after publishing must not leak through the snapshot.
    demuxer->mutateVideoInfoInPlace();
    auto video = component.videoInfo();
    ASSERT_NE(video, nullptr);
    EXPECT_EQ(video->width, 1920);
    EXPECT_EQ(video->fps, 30);
    ASSERT_NE(seenInCallback, nullptr);
    EXPECT_EQ(seenInCallback->width, 1920);

    // Q5: stop clears the snapshot; previously copied values stay readable.
    component.stop();
    EXPECT_FALSE(component.hasVideo());
    EXPECT_EQ(component.videoInfo(), nullptr);
    EXPECT_EQ(seenInCallback->width, 1920);
    EXPECT_EQ(demuxer->overlaps(), 0);
}

// Q2: TS-like startup — open publishes an empty header before any stream info exists;
// the first parseData assigns the info. The result callback (running on the worker)
// hands the observation to another thread, mirroring postToOwnerThread.
TEST(DemuxerComponentTest, TsStartupSnapshotRaceRegression) {    class TSLikeDemuxer final : public IDemuxer {
     public:
        TSLikeDemuxer() { type_ = DemuxerType::TS; }

        bool open(std::unique_ptr<Buffer>&) noexcept override {
            // TS startup: open succeeds before any stream info has been parsed.
            isOpened_ = true;
            return true;
        }
        void close() noexcept override {}

        DemuxerResult parseData(DataPacket&) noexcept override {
            bool expected = false;
            if (infoPublished_.compare_exchange_strong(expected, true)) {
                auto video = std::make_shared<VideoInfo>();
                video->width = 640;
                video->height = 480;
                videoInfo_ = video;
            }
            DemuxerResult result;
            result.resultCode = DemuxerResultCode::ParsedHeader;
            return result;
        }

        uint64_t getSeekToPos(double) noexcept override { return 0; }

     private:
        std::atomic<bool> infoPublished_{false};
    };

    constexpr int kRounds = 500;
    for (int round = 0; round < kRounds; ++round) {
        auto [sender, receiver] = Channel<DataPacket>::create();
        DemuxerConfig config;  // Unknown type: the probe path publishes the open-phase header
        DemuxerComponent component(std::move(receiver), std::move(config));
        auto demuxer = std::make_shared<TSLikeDemuxer>();
        component.setDemuxer(demuxer);

        std::mutex mutex;
        std::condition_variable condition;
        int headersSeen = 0;
        bool firstHasVideo = true;
        bool secondHasVideo = false;
        std::atomic<bool> stopPoller{false};
        std::atomic<bool> corrupted{false};
        // A concurrent reader mirrors the UI thread: hasVideo()==true must always imply
        // a fully initialized snapshot (this is the race TSan flags on the old design).
        std::thread poller([&] {
            while (!stopPoller.load(std::memory_order_acquire)) {
                if (component.hasVideo()) {
                    auto video = component.videoInfo();
                    if (!video || video->width == 0 || video->height == 0) {
                        corrupted.store(true, std::memory_order_release);
                    }
                }
                std::this_thread::sleep_for(100us);
            }
        });

        // Synchronous read in the callback (worker): publication strictly precedes the
        // callback, so the open-phase read must see an empty snapshot and the
        // post-parse read the published info, in deterministic order.
        component.setHandleResultFunc(
            [&, &component = component](const std::shared_ptr<IDemuxer>&, DemuxerResult&&) {
                const auto hasVideo = component.hasVideo();
                std::lock_guard lock(mutex);
                if (headersSeen == 0) {
                    firstHasVideo = hasVideo;
                } else if (headersSeen == 1) {
                    secondHasVideo = hasVideo;
                }
                ++headersSeen;
                condition.notify_all();
            });

        component.start();
        // Packet A triggers the probe/open path (empty header); packet B reaches parseData.
        for (const auto offset : {0u, 1u}) {
            DataPacket packet;
            packet.data = Data::create(16);
            packet.offset = offset;
            packet.sourceGeneration = 1;
            ASSERT_TRUE(sender->send(std::move(packet)).has_value()) << "round " << round;
        }

        std::unique_lock lock(mutex);
        const bool ok = condition.wait_for(lock, std::chrono::seconds(5), [&] {
            return headersSeen >= 2;
        });
        stopPoller.store(true, std::memory_order_release);
        lock.unlock();
        poller.join();
        ASSERT_TRUE(ok) << "round " << round << ": headersSeen=" << headersSeen;
        EXPECT_FALSE(corrupted.load()) << "round " << round << ": half-initialized read";
        EXPECT_FALSE(firstHasVideo)
            << "round " << round << ": open-phase header must see an empty snapshot";
        EXPECT_TRUE(secondHasVideo)
            << "round " << round << ": post-parse header must see the published info";

        component.stop();
    }
}

// Q4: with the real Sample.mp4, a reader that starts polling before any header is
// published must never observe a half-initialized VideoInfo.
TEST(DemuxerComponentTest, RealMp4SnapshotNeverHalfInitialized) {
    {
        std::ifstream input(kMp4Path, std::ios::binary);
        if (!input) {
            GTEST_SKIP() << "Sample.mp4 not found, skipping";
        }
    }

    constexpr int kRounds = 50;
    for (int round = 0; round < kRounds; ++round) {
        // MP4 uses the probe path in production (preferredType stays Unknown): packet bytes
        // feed probeBuffer_ and open() parses the moov from them. Sample.mp4 keeps the moov
        // at the file tail, so the whole file is needed before open() succeeds.
        auto mp4Data = loadData(kMp4Path, 0, 8 * 1024 * 1024);
        if (!mp4Data) {
            GTEST_SKIP() << "Sample.mp4 not found, skipping";
        }
        auto [sender, receiver] = Channel<DataPacket>::create();
        DemuxerConfig config;
        DemuxerComponent component(std::move(receiver), std::move(config));

        std::atomic<bool> stopReader{false};
        std::atomic<bool> observed{false};
        std::atomic<bool> corrupted{false};
        std::thread reader([&] {
            while (!stopReader.load(std::memory_order_acquire)) {
                if (auto video = component.videoInfo()) {
                    observed.store(true, std::memory_order_release);
                    if (video->width == 0 || video->height == 0) {
                        corrupted.store(true, std::memory_order_release);
                    }
                    return;
                }
                std::this_thread::sleep_for(100us);
            }
        });

        component.start();
        {
            DataPacket packet;
            packet.data = std::move(mp4Data);
            packet.offset = 0;
            packet.sourceGeneration = 1;
            ASSERT_TRUE(sender->send(std::move(packet)).has_value()) << "round " << round;
        }

        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (!observed.load() && std::chrono::steady_clock::now() < deadline) {
            std::this_thread::yield();
        }
        stopReader.store(true, std::memory_order_release);
        reader.join();
        ASSERT_TRUE(observed.load()) << "round " << round << ": header never published";
        EXPECT_FALSE(corrupted.load()) << "round " << round << ": half-initialized VideoInfo";

        component.stop();
    }
}

// D8: a paused demuxer worker must sleep on the condition variable; 300ms of wall
// time may not consume more than 30ms of process CPU (2ms polling would burn ~all of it).
TEST(DemuxerComponentTest, PausedWorkerDoesNotBurnCpu) {
    auto processCpuTimeUs = [] {
        rusage usage{};
        getrusage(RUSAGE_SELF, &usage);
        return static_cast<int64_t>(usage.ru_utime.tv_sec + usage.ru_stime.tv_sec) * 1'000'000 +
               usage.ru_utime.tv_usec + usage.ru_stime.tv_usec;
    };

    auto [sender, receiver] = Channel<DataPacket>::create();
    DemuxerConfig config;
    DemuxerComponent component(std::move(receiver), std::move(config));
    component.start();
    component.pause();

    const auto before = processCpuTimeUs();
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    const auto used = processCpuTimeUs() - before;
    component.stop();

    EXPECT_LE(used, 30'000) << "paused worker consumed " << used << "us of CPU in 300ms";
    (void)sender;
}
