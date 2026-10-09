//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//
#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>
#include <memory>
#include <vector>

#include "Buffer.hpp"
#include "Data.h"
#include "Mp4Box.h"
#include "Mp4TestBytes.h"

using namespace slark;

namespace {

using slark::test::be32;
using slark::test::be64;
using slark::test::Bytes;
using slark::test::fourcc;
using slark::test::makeBuffer;

BoxInfo infoFor(const Bytes& payload) {
    BoxInfo info;
    info.size = payload.size() + 8;
    return info;
}

}  // namespace

TEST(Mp4MalformedBoxTest, LargesizeZeroIsRejected) {
    Bytes bytes;
    be32(bytes, 1);
    fourcc(bytes, "free");
    be64(bytes, 0);
    auto buffer = makeBuffer(bytes);
    EXPECT_EQ(Box::createBox(*buffer), nullptr);
    EXPECT_EQ(buffer->pos(), 0u);
}

TEST(Mp4MalformedBoxTest, LargesizeBelowHeaderIsRejected) {
    Bytes bytes;
    be32(bytes, 1);
    fourcc(bytes, "free");
    be64(bytes, 8);
    auto buffer = makeBuffer(bytes);
    EXPECT_EQ(Box::createBox(*buffer), nullptr);
    EXPECT_EQ(buffer->pos(), 0u);
}

TEST(Mp4MalformedBoxTest, SizeBelowHeaderIsRejected) {
    for (uint32_t size : {2u, 4u, 7u}) {
        Bytes bytes;
        be32(bytes, size);
        fourcc(bytes, "free");
        bytes.resize(16, 0);
        auto buffer = makeBuffer(bytes);
        EXPECT_EQ(Box::createBox(*buffer), nullptr) << "size " << size;
        EXPECT_EQ(buffer->pos(), 0u) << "size " << size;
    }
}

TEST(Mp4MalformedBoxTest, ValidMinimalBoxStillParses) {
    Bytes bytes;
    be32(bytes, 8);
    fourcc(bytes, "free");
    auto buffer = makeBuffer(bytes);
    auto box = Box::createBox(*buffer);
    ASSERT_NE(box, nullptr);
    EXPECT_EQ(box->info.size, 8u);
}

TEST(Mp4MalformedBoxTest, SizeZeroWithUnknownTotalExtendsToBufferEnd) {
    Bytes bytes;
    be32(bytes, 0);
    fourcc(bytes, "mdat");
    bytes.resize(18, 0xAB);
    auto buffer = makeBuffer(bytes);
    auto box = Box::createBox(*buffer);
    ASSERT_NE(box, nullptr);
    EXPECT_EQ(box->info.size, 18u);
    EXPECT_EQ(box->info.end(), 18u);
}

TEST(Mp4MalformedBoxTest, SizeZeroWithKnownTotalUsesTotal) {
    Bytes bytes;
    be32(bytes, 0);
    fourcc(bytes, "mdat");
    bytes.resize(18, 0xAB);
    auto buffer = makeBuffer(bytes, 100);
    auto box = Box::createBox(*buffer);
    ASSERT_NE(box, nullptr);
    EXPECT_EQ(box->info.size, 100u);
}

TEST(Mp4MalformedBoxTest, ElstHugeEntryCountFailsWithoutAllocating) {
    Bytes payload;
    be32(payload, 0);  // version 0 + flags
    be32(payload, 0xFFFFFFFFu);
    auto buffer = makeBuffer(payload);
    BoxElst box(infoFor(payload));
    EXPECT_FALSE(box.decode(*buffer));
    EXPECT_EQ(box.entries.capacity(), 0u);
}

TEST(Mp4MalformedBoxTest, ElstVersion1RequiresTwentyBytesPerEntry) {
    Bytes payload;
    be32(payload, 0x01000000u);  // version 1
    be32(payload, 2);            // claims two 20-byte entries
    payload.resize(payload.size() + 20, 0);
    auto buffer = makeBuffer(payload);
    BoxElst box(infoFor(payload));
    EXPECT_FALSE(box.decode(*buffer));
}

TEST(Mp4MalformedBoxTest, TrunHugeSampleCountWithoutPerSampleFieldsFails) {
    Bytes payload;
    be32(payload, 0);  // version 0, flags 0: no per-sample fields
    be32(payload, 0xFFFFFFFFu);
    auto buffer = makeBuffer(payload);
    BoxTrun box(infoFor(payload));
    EXPECT_FALSE(box.decode(*buffer));
    EXPECT_EQ(box.samples.capacity(), 0u);
}

TEST(Mp4MalformedBoxTest, TrunHugeSampleCountWithFieldsFailsWithoutAllocating) {
    Bytes payload;
    be32(payload, BoxTrun::kSampleDurationPresent | BoxTrun::kSampleSizePresent);
    be32(payload, 0xFFFFFFFFu);
    auto buffer = makeBuffer(payload);
    BoxTrun box(infoFor(payload));
    EXPECT_FALSE(box.decode(*buffer));
    EXPECT_EQ(box.samples.capacity(), 0u);
}

TEST(Mp4MalformedBoxTest, TrunSmallCountWithoutPerSampleFieldsIsAccepted) {
    Bytes payload;
    be32(payload, 0);
    be32(payload, 100);
    auto buffer = makeBuffer(payload);
    BoxTrun box(infoFor(payload));
    EXPECT_TRUE(box.decode(*buffer));
    EXPECT_EQ(box.samples.size(), 100u);
}

TEST(Mp4MalformedBoxTest, StszCountBeyondPayloadFailsWithoutAllocating) {
    Bytes payload;
    be32(payload, 0);        // version + flags
    be32(payload, 0);        // default sample size
    be32(payload, 5000000);  // entry count, but no entries follow
    auto buffer = makeBuffer(payload);
    BoxStsz box(infoFor(payload));
    EXPECT_FALSE(box.decode(*buffer));
    EXPECT_EQ(box.sampleSizes.capacity(), 0u);
}

TEST(Mp4MalformedBoxTest, StcoCountBeyondPayloadFailsWithoutAllocating) {
    Bytes payload;
    be32(payload, 0);
    be32(payload, 5000000);
    auto buffer = makeBuffer(payload);
    BoxStco box(infoFor(payload));
    EXPECT_FALSE(box.decode(*buffer));
    EXPECT_EQ(box.chunkOffsets.capacity(), 0u);
}

TEST(Mp4MalformedBoxTest, Co64CountBeyondPayloadFailsWithoutAllocating) {
    Bytes payload;
    be32(payload, 0);
    be32(payload, 5000000);
    auto buffer = makeBuffer(payload);
    BoxCo64 box(infoFor(payload));
    EXPECT_FALSE(box.decode(*buffer));
    EXPECT_EQ(box.chunkOffsets.capacity(), 0u);
}

TEST(Mp4MalformedBoxTest, StscCountBeyondPayloadFailsWithoutAllocating) {
    Bytes payload;
    be32(payload, 0);
    be32(payload, 5000000);
    auto buffer = makeBuffer(payload);
    BoxStsc box(infoFor(payload));
    EXPECT_FALSE(box.decode(*buffer));
    EXPECT_EQ(box.entrys.capacity(), 0u);
}

TEST(Mp4MalformedBoxTest, StszWellFormedTableStillDecodes) {
    Bytes payload;
    be32(payload, 0);
    be32(payload, 0);
    be32(payload, 3);
    be32(payload, 10);
    be32(payload, 20);
    be32(payload, 30);
    auto buffer = makeBuffer(payload);
    BoxStsz box(infoFor(payload));
    ASSERT_TRUE(box.decode(*buffer));
    ASSERT_EQ(box.sampleSizes.size(), 3u);
    EXPECT_EQ(box.sampleSizes[2], 30u);
}
