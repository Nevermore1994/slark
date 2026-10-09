//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//
#include <gtest/gtest.h>

#include <chrono>
#include <cstdint>
#include <memory>

#include "AVFrame.hpp"
#include "Mp4TestBytes.h"
#include "demuxer/Mp4Demuxer.h"

using namespace slark;
using namespace std::chrono_literals;
using slark::test::be16;
using slark::test::be32;
using slark::test::box;
using slark::test::Bytes;
using slark::test::concat;
using slark::test::fourcc;
using slark::test::makeBuffer;
using slark::test::makeData;
using slark::test::runDetached;

namespace {

Bytes ftypBox() {
    Bytes payload;
    fourcc(payload, "isom");
    be32(payload, 0x200);
    fourcc(payload, "isom");
    return box("ftyp", payload);
}

Bytes mvhdBox(uint32_t timeScale, uint32_t duration) {
    Bytes payload;
    be32(payload, 0);  // version + flags
    be32(payload, 0);  // creation time
    be32(payload, 0);  // modification time
    be32(payload, timeScale);
    be32(payload, duration);
    be32(payload, 0x00010000);  // rate
    be16(payload, 0x0100);      // volume
    payload.resize(payload.size() + 10, 0);
    for (uint32_t m : {0x00010000u, 0u, 0u, 0u, 0x00010000u, 0u, 0u, 0u, 0x40000000u}) {
        be32(payload, m);
    }
    payload.resize(payload.size() + 24, 0);
    be32(payload, 2);  // next track id
    return box("mvhd", payload);
}

// Returns open() result, or nullopt when open() did not finish in time.
std::optional<bool> openWithTimeout(Bytes file) {
    auto result = runDetached<bool>([file = std::move(file)]() {
        auto demuxer = std::make_shared<Mp4Demuxer>();
        auto buffer = makeBuffer(file);
        return demuxer->open(buffer);
    });
    if (result.wait_for(5s) != std::future_status::ready) {
        return std::nullopt;
    }
    return result.get();
}

AVFramePtrArray parseH264(uint16_t naluByteSize, const Bytes& payload) {
    TrackContext track;
    track.codecId = CodecId::AVC;
    track.naluByteSize = naluByteSize;
    auto frame = std::make_unique<AVFrame>(AVFrameType::Video);
    frame->timeScale = 1000;
    frame->duration = 1;
    return track.parseH264FrameData(std::move(frame), makeData(payload),
                                    std::make_shared<VideoFrameInfo>());
}

}  // namespace

TEST(Mp4MalformedDemuxerTest, MoovWithoutMvhdFailsToOpen) {
    auto result = openWithTimeout(concat({ftypBox(), box("moov", {})}));
    ASSERT_TRUE(result.has_value()) << "open() did not finish";
    EXPECT_FALSE(*result);
}

TEST(Mp4MalformedDemuxerTest, MoovWithZeroTimeScaleAndNoTracksFailsToOpen) {
    auto result = openWithTimeout(concat({ftypBox(), box("moov", mvhdBox(0, 1000))}));
    ASSERT_TRUE(result.has_value()) << "open() did not finish";
    EXPECT_FALSE(*result);
}

TEST(Mp4MalformedDemuxerTest, MoovWithValidMvhdButNoTracksFailsToOpen) {
    auto result = openWithTimeout(concat({ftypBox(), box("moov", mvhdBox(1000, 5000))}));
    ASSERT_TRUE(result.has_value()) << "open() did not finish";
    EXPECT_FALSE(*result);
}

TEST(Mp4MalformedDemuxerTest, MoovChildWithZeroLargesizeDoesNotLoopForever) {
    Bytes badChild;
    be32(badChild, 1);
    fourcc(badChild, "free");
    be32(badChild, 0);
    be32(badChild, 0);
    auto result = openWithTimeout(concat({ftypBox(), box("moov", concat({mvhdBox(1000, 1), badChild}))}));
    ASSERT_TRUE(result.has_value()) << "open() looped forever";
    EXPECT_FALSE(*result);
}

TEST(Mp4MalformedDemuxerTest, MoovChildWithTinySizeDoesNotLoopForever) {
    Bytes badChild;
    be32(badChild, 3);
    fourcc(badChild, "free");
    badChild.resize(16, 0);
    auto result = openWithTimeout(concat({ftypBox(), box("moov", concat({mvhdBox(1000, 1), badChild}))}));
    ASSERT_TRUE(result.has_value()) << "open() looped forever";
    EXPECT_FALSE(*result);
}

TEST(Mp4MalformedDemuxerTest, NaluLengthWrappingAroundIsRejected) {
    auto result = runDetached<size_t>([] {
        // 0xFFFFFFFC + 4-byte prefix wraps to 0 in 32-bit arithmetic
        return parseH264(3, {0xFF, 0xFF, 0xFF, 0xFC, 0x65, 0xAA, 0xBB, 0xCC}).size();
    });
    ASSERT_EQ(result.wait_for(5s), std::future_status::ready) << "parseNaluFrames looped forever";
    EXPECT_EQ(result.get(), 0u);
}

TEST(Mp4MalformedDemuxerTest, ZeroLengthNaluIsRejected) {
    auto result = runDetached<size_t>([] {
        return parseH264(3, {0x00, 0x00, 0x00, 0x00, 0x65, 0xAA}).size();
    });
    ASSERT_EQ(result.wait_for(5s), std::future_status::ready) << "parseNaluFrames looped forever";
    EXPECT_EQ(result.get(), 0u);
}

TEST(Mp4MalformedDemuxerTest, TruncatedNaluIsRejected) {
    EXPECT_TRUE(parseH264(3, {0x00, 0x00, 0x00, 0x10, 0x65, 0xAA}).empty());
}

TEST(Mp4MalformedDemuxerTest, FourByteLengthPrefixYieldsOneFramePerNalu) {
    auto frames = parseH264(3, {0x00, 0x00, 0x00, 0x03, 0x65, 0xAA, 0xBB, 0x00, 0x00, 0x00, 0x02,
                                0x41, 0xCC});
    ASSERT_EQ(frames.size(), 2u);
    EXPECT_TRUE(frames[0]->videoInfo()->isIDRFrame);
    EXPECT_FALSE(frames[1]->videoInfo()->isIDRFrame);
}

TEST(Mp4MalformedDemuxerTest, TwoByteLengthPrefixReadsNaluHeaderRightAfterLength) {
    // NALU header sits at pos + prefix size; a fixed +4 offset would read payload bytes instead
    auto frames = parseH264(1, {0x00, 0x03, 0x65, 0xAA, 0xBB, 0x00, 0x02, 0x41, 0xCC});
    ASSERT_EQ(frames.size(), 2u);
    EXPECT_TRUE(frames[0]->videoInfo()->isIDRFrame);
    EXPECT_EQ(frames[0]->data->length(), 5u);
    EXPECT_FALSE(frames[1]->videoInfo()->isIDRFrame);
    EXPECT_EQ(frames[1]->data->length(), 4u);
}
