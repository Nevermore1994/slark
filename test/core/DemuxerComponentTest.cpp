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
#include <vector>

#include "Data.h"
#include "DemuxerComponent.h"

using namespace slark;

namespace {

constexpr const char* kTsPath = "../../test_sample/segment-0.ts";

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
};

}  // namespace

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
