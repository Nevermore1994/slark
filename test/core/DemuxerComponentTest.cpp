//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <fstream>
#include <memory>
#include <mutex>
#include <string_view>
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
    bool demuxerValid = true;
    std::vector<DemuxerResultCode> resultCodes;
    std::vector<bool> resultHasFrames;
};

}  // namespace

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

    DataPacket packet;
    packet.data = std::move(data);
    packet.offset = 0;
    packet.segmentIndex = 4;
    ASSERT_TRUE(sender->send(std::move(packet)).has_value());

    DataPacket eos;
    eos.isEndOfStream = true;
    ASSERT_TRUE(sender->send(std::move(eos)).has_value());

    {
        std::unique_lock lock(state.mutex);
        ASSERT_TRUE(state.condition.wait_for(lock, std::chrono::seconds(10), [&state] {
            return state.fileEnds == 1;
        }));
        ASSERT_GE(state.parsedHeaders, 1u);
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
