//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#include "MediaSourceManager.h"

#include <gtest/gtest.h>

#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <mutex>
#include <string>
#include <vector>

using namespace slark;

namespace {

std::string samplePath() {
    for (const auto* path : {"../../test_sample/sample-3s.wav", "test_sample/sample-3s.wav"}) {
        if (std::filesystem::exists(path)) {
            return path;
        }
    }
    return "../../test_sample/sample-3s.wav";
}

struct CallbackState {
    std::mutex mutex;
    std::condition_variable condition;
    std::vector<DataPacket> packets;
    std::vector<std::string> events;
    bool completed = false;
    bool failed = false;
};

bool openWithCallbacks(MediaSourceManager& manager, CallbackState& state) {
    const auto path = samplePath();
    return manager.open(
        path,
        [&state](DataPacket packet) {
            std::lock_guard lock(state.mutex);
            state.events.emplace_back(packet.isEndOfStream ? "eos" : "data");
            state.packets.emplace_back(std::move(packet));
            state.condition.notify_all();
        },
        [&state](Error, int32_t) {
            std::lock_guard lock(state.mutex);
            state.failed = true;
            state.condition.notify_all();
        },
        [&state]() {
            std::lock_guard lock(state.mutex);
            state.events.emplace_back("complete");
            state.completed = true;
            state.condition.notify_all();
        });
}

}  // namespace

class MediaSourceManagerTest : public ::testing::Test {
 protected:
    void TearDown() override {
        manager_.close();
    }

    MediaSourceManager manager_;
};

TEST_F(MediaSourceManagerTest, ConvertsDataChunksAndPreservesEosOrdering) {
    CallbackState state;

    ASSERT_TRUE(openWithCallbacks(manager_, state));

    const auto info = manager_.streamInfo();
    EXPECT_TRUE(manager_.isValid());
    EXPECT_EQ(info.type, StreamType::File);
    EXPECT_EQ(info.uri, samplePath());
    EXPECT_GT(info.totalSize, 0u);
    EXPECT_TRUE(info.isSeekable);
    EXPECT_FALSE(info.isLive);

    manager_.start();

    {
        std::unique_lock lock(state.mutex);
        ASSERT_TRUE(state.condition.wait_for(lock, std::chrono::seconds(10), [&state] {
            return state.completed;
        }));
        ASSERT_FALSE(state.failed);
        ASSERT_GE(state.packets.size(), 2u);
        ASSERT_EQ(state.events.back(), "complete");
        ASSERT_EQ(state.events[state.events.size() - 2], "eos");

        for (size_t i = 0; i + 1 < state.packets.size(); ++i) {
            const auto& packet = state.packets[i];
            ASSERT_NE(packet.data, nullptr);
            EXPECT_FALSE(packet.isEndOfStream);
            EXPECT_EQ(packet.segmentIndex, 0u);
            EXPECT_EQ(packet.tag, "0");
            EXPECT_FALSE(packet.isLastChunk);
            EXPECT_GT(packet.length(), 0u);
            if (i > 0) {
                EXPECT_GT(packet.offset, state.packets[i - 1].offset);
            }
        }

        const auto& eos = state.packets.back();
        EXPECT_EQ(state.events[state.events.size() - 2], "eos");
        EXPECT_EQ(eos.data, nullptr);
        EXPECT_EQ(eos.length(), 0u);
        EXPECT_TRUE(eos.empty());
        EXPECT_TRUE(eos.isEndOfStream);
        EXPECT_FALSE(eos.isLastChunk);
        EXPECT_EQ(eos.segmentIndex, 0u);
        EXPECT_TRUE(eos.tag.empty());
    }
}

TEST_F(MediaSourceManagerTest, ConvertsRangeEndMetadataToLastChunkPacket) {
    CallbackState state;
    ASSERT_TRUE(openWithCallbacks(manager_, state));

    manager_.setReadRange(Range(0, 1));
    manager_.start();

    {
        std::unique_lock lock(state.mutex);
        ASSERT_TRUE(state.condition.wait_for(lock, std::chrono::seconds(10), [&state] {
            return state.completed;
        }));
        ASSERT_FALSE(state.failed);
        ASSERT_EQ(state.packets.size(), 2u);

        const auto& data = state.packets.front();
        EXPECT_NE(data.data, nullptr);
        EXPECT_EQ(data.length(), 1u);
        EXPECT_EQ(data.offset, 0);
        EXPECT_EQ(data.segmentIndex, 0u);
        EXPECT_EQ(data.tag, "0");
        EXPECT_TRUE(data.isLastChunk);
        EXPECT_FALSE(data.isEndOfStream);

        const auto& eos = state.packets.back();
        EXPECT_TRUE(eos.isEndOfStream);
        EXPECT_FALSE(eos.isLastChunk);
    }
}
