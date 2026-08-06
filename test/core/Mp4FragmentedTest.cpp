//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#include <gtest/gtest.h>

#include <vector>

#include "Buffer.hpp"
#include "demuxer/Mp4Box.h"
#include "demuxer/Mp4Demuxer.h"

using namespace slark;

namespace {

std::unique_ptr<Buffer> makeBuffer(std::vector<uint8_t> bytes) {
    auto data = Data::create(bytes.size());
    std::memcpy(data->data(), bytes.data(), bytes.size());
    data->setSize(bytes.size());
    auto buf = std::make_unique<Buffer>();
    buf->append(0, std::move(data));
    return buf;
}

void write4BE(std::vector<uint8_t>& v, uint32_t val) {
    v.push_back(static_cast<uint8_t>(val >> 24));
    v.push_back(static_cast<uint8_t>(val >> 16));
    v.push_back(static_cast<uint8_t>(val >> 8));
    v.push_back(static_cast<uint8_t>(val));
}

void write8BE(std::vector<uint8_t>& v, uint64_t val) {
    write4BE(v, static_cast<uint32_t>(val >> 32));
    write4BE(v, static_cast<uint32_t>(val));
}

void write2BE(std::vector<uint8_t>& v, uint16_t val) {
    v.push_back(static_cast<uint8_t>(val >> 8));
    v.push_back(static_cast<uint8_t>(val));
}

}


TEST(BoxElstTest, DecodeV0SingleEntry) {
    std::vector<uint8_t> data;
    data.push_back(0);
    data.push_back(0);
    data.push_back(0);
    data.push_back(0);
    write4BE(data, 1);
    write4BE(data, 3000);
    write4BE(data, 512);
    write2BE(data, 1);
    write2BE(data, 0);

    BoxInfo info;
    info.size = data.size() + 8;
    BoxElst box(std::move(info));
    auto buf = makeBuffer(data);
    ASSERT_TRUE(box.decode(*buf));
    ASSERT_EQ(box.entries.size(), 1u);
    EXPECT_EQ(box.entries[0].segmentDuration, 3000u);
    EXPECT_EQ(box.entries[0].mediaTime, 512);
    EXPECT_EQ(box.entries[0].mediaRateInteger, 1);
    EXPECT_EQ(box.mediaTimeOffset(), 512);
    EXPECT_FALSE(box.hasEmptyEdit());
}

TEST(BoxElstTest, DecodeV0EmptyEdit) {
    std::vector<uint8_t> data;
    data.push_back(0);
    data.push_back(0);
    data.push_back(0);
    data.push_back(0);
    write4BE(data, 2);
    write4BE(data, 1000);
    write4BE(data, static_cast<uint32_t>(-1));
    write2BE(data, 1);
    write2BE(data, 0);
    write4BE(data, 5000);
    write4BE(data, 0);
    write2BE(data, 1);
    write2BE(data, 0);

    BoxInfo info;
    info.size = data.size() + 8;
    BoxElst box(std::move(info));
    auto buf = makeBuffer(data);
    ASSERT_TRUE(box.decode(*buf));
    ASSERT_EQ(box.entries.size(), 2u);
    EXPECT_TRUE(box.hasEmptyEdit());
    EXPECT_EQ(box.mediaTimeOffset(), 0);
}

TEST(BoxElstTest, DecodeV1Entry) {
    std::vector<uint8_t> data;
    data.push_back(1);
    data.push_back(0);
    data.push_back(0);
    data.push_back(0);
    write4BE(data, 1);
    write8BE(data, 90000ULL);
    write8BE(data, 1024ULL);
    write2BE(data, 1);
    write2BE(data, 0);

    BoxInfo info;
    info.size = data.size() + 8;
    BoxElst box(std::move(info));
    auto buf = makeBuffer(data);
    ASSERT_TRUE(box.decode(*buf));
    ASSERT_EQ(box.entries.size(), 1u);
    EXPECT_EQ(box.entries[0].segmentDuration, 90000u);
    EXPECT_EQ(box.entries[0].mediaTime, 1024);
    EXPECT_EQ(box.mediaTimeOffset(), 1024);
}


TEST(BoxTrexTest, Decode) {
    std::vector<uint8_t> data;
    data.push_back(0);
    data.push_back(0);
    data.push_back(0);
    data.push_back(0);
    write4BE(data, 1);
    write4BE(data, 1);
    write4BE(data, 512);
    write4BE(data, 1024);
    write4BE(data, 0);

    BoxInfo info;
    info.size = data.size() + 8;
    BoxTrex box(std::move(info));
    auto buf = makeBuffer(data);
    ASSERT_TRUE(box.decode(*buf));
    EXPECT_EQ(box.trackId, 1u);
    EXPECT_EQ(box.defaultSampleDuration, 512u);
    EXPECT_EQ(box.defaultSampleSize, 1024u);
}


TEST(BoxTfhdTest, DecodeMinimal) {
    std::vector<uint8_t> data;
    data.push_back(0);
    data.push_back(0);
    data.push_back(0);
    data.push_back(0);
    write4BE(data, 2);

    BoxInfo info;
    info.size = data.size() + 8;
    BoxTfhd box(std::move(info));
    auto buf = makeBuffer(data);
    ASSERT_TRUE(box.decode(*buf));
    EXPECT_EQ(box.trackId, 2u);
    EXPECT_EQ(box.flags, 0u);
}

TEST(BoxTfhdTest, DecodeWithDefaultSampleDuration) {
    std::vector<uint8_t> data;
    data.push_back(0);
    data.push_back(0);
    data.push_back(0);
    data.push_back(0x08);
    write4BE(data, 1);
    write4BE(data, 3000);

    BoxInfo info;
    info.size = data.size() + 8;
    BoxTfhd box(std::move(info));
    auto buf = makeBuffer(data);
    ASSERT_TRUE(box.decode(*buf));
    EXPECT_EQ(box.trackId, 1u);
    EXPECT_EQ(box.defaultSampleDuration, 3000u);
}


TEST(BoxTfdtTest, DecodeV0) {
    std::vector<uint8_t> data;
    data.push_back(0);
    data.push_back(0);
    data.push_back(0);
    data.push_back(0);
    write4BE(data, 90000);

    BoxInfo info;
    info.size = data.size() + 8;
    BoxTfdt box(std::move(info));
    auto buf = makeBuffer(data);
    ASSERT_TRUE(box.decode(*buf));
    EXPECT_EQ(box.baseMediaDecodeTime, 90000u);
}

TEST(BoxTfdtTest, DecodeV1) {
    std::vector<uint8_t> data;
    data.push_back(1);
    data.push_back(0);
    data.push_back(0);
    data.push_back(0);
    write8BE(data, 9000000000ULL);

    BoxInfo info;
    info.size = data.size() + 8;
    BoxTfdt box(std::move(info));
    auto buf = makeBuffer(data);
    ASSERT_TRUE(box.decode(*buf));
    EXPECT_EQ(box.baseMediaDecodeTime, 9000000000ULL);
}


TEST(BoxTrunTest, DecodeWithSizeAndDuration) {
    constexpr uint32_t kFlags =
        BoxTrun::kDataOffsetPresent | BoxTrun::kSampleDurationPresent | BoxTrun::kSampleSizePresent;
    std::vector<uint8_t> data;
    data.push_back(0);
    data.push_back(static_cast<uint8_t>(kFlags >> 16));
    data.push_back(static_cast<uint8_t>(kFlags >> 8));
    data.push_back(static_cast<uint8_t>(kFlags));
    write4BE(data, 3);
    write4BE(data, 8);
    for (int i = 0; i < 3; ++i) {
        write4BE(data, 512);
        write4BE(data, 1024);
    }

    BoxInfo info;
    info.size = data.size() + 8;
    BoxTrun box(std::move(info));
    auto buf = makeBuffer(data);
    ASSERT_TRUE(box.decode(*buf));
    ASSERT_EQ(box.samples.size(), 3u);
    EXPECT_EQ(box.dataOffset, 8);
    EXPECT_EQ(box.samples[0].duration, 512u);
    EXPECT_EQ(box.samples[0].size, 1024u);
}


TEST(BoxSidxTest, DecodeV0) {
    std::vector<uint8_t> data;
    data.push_back(0);
    data.push_back(0);
    data.push_back(0);
    data.push_back(0);
    write4BE(data, 1);
    write4BE(data, 90000);
    write4BE(data, 0);
    write4BE(data, 0);
    write2BE(data, 0);
    write2BE(data, 2);
    write4BE(data, 1000);
    write4BE(data, 9000);
    write4BE(data, (1u << 31) | (1u << 28));
    write4BE(data, 2000);
    write4BE(data, 9000);
    write4BE(data, (1u << 31) | (1u << 28));

    BoxInfo info;
    info.size = data.size() + 8;
    BoxSidx box(std::move(info));
    auto buf = makeBuffer(data);
    ASSERT_TRUE(box.decode(*buf));
    EXPECT_EQ(box.referenceId, 1u);
    EXPECT_EQ(box.timeScale, 90000u);
    ASSERT_EQ(box.entries.size(), 2u);
    EXPECT_EQ(box.entries[0].referencedSize, 1000u);
    EXPECT_EQ(box.entries[0].subsegmentDuration, 9000u);
    EXPECT_TRUE(box.entries[0].startsWithSap);
    EXPECT_EQ(box.entries[0].sapType, 1u);
    EXPECT_FALSE(box.entries[0].referenceType);
}


TEST(BoxMfhdTest, Decode) {
    std::vector<uint8_t> data;
    data.push_back(0);
    data.push_back(0);
    data.push_back(0);
    data.push_back(0);
    write4BE(data, 42);

    BoxInfo info;
    info.size = data.size() + 8;
    BoxMfhd box(std::move(info));
    auto buf = makeBuffer(data);
    ASSERT_TRUE(box.decode(*buf));
    EXPECT_EQ(box.sequenceNumber, 42u);
}


TEST(BoxFactoryTest, CreateElstBox) {
    std::vector<uint8_t> raw;
    write4BE(raw,
             8 + 4 + 4 + 12);
    raw.push_back('e');
    raw.push_back('l');
    raw.push_back('s');
    raw.push_back('t');
    raw.push_back(0);
    raw.push_back(0);
    raw.push_back(0);
    raw.push_back(0);
    write4BE(raw, 1);
    write4BE(raw, 1000);
    write4BE(raw, 100);
    write2BE(raw, 1);
    write2BE(raw, 0);

    auto buf = makeBuffer(raw);
    auto box = Box::createBox(*buf);
    ASSERT_NE(box, nullptr);
    EXPECT_EQ(box->info.symbol, "elst");
    auto elst = std::dynamic_pointer_cast<BoxElst>(box);
    ASSERT_NE(elst, nullptr);
    elst->decode(*buf);
    ASSERT_EQ(elst->entries.size(), 1u);
    EXPECT_EQ(elst->entries[0].mediaTime, 100);
}

TEST(BoxFactoryTest, CreateMoofBox) {
    std::vector<uint8_t> raw;
    write4BE(raw, 8);
    raw.push_back('m');
    raw.push_back('o');
    raw.push_back('o');
    raw.push_back('f');

    auto buf = makeBuffer(raw);
    auto box = Box::createBox(*buf);
    ASSERT_NE(box, nullptr);
    EXPECT_EQ(box->info.symbol, "moof");
    EXPECT_NE(std::dynamic_pointer_cast<BoxMoof>(box), nullptr);
}

TEST(BoxFactoryTest, CreateTrunBox) {
    std::vector<uint8_t> raw;
    write4BE(raw, 8);
    raw.push_back('t');
    raw.push_back('r');
    raw.push_back('u');
    raw.push_back('n');

    auto buf = makeBuffer(raw);
    auto box = Box::createBox(*buf);
    ASSERT_NE(box, nullptr);
    EXPECT_EQ(box->info.symbol, "trun");
    EXPECT_NE(std::dynamic_pointer_cast<BoxTrun>(box), nullptr);
}

TEST(BoxFactoryTest, CreateSidxBox) {
    std::vector<uint8_t> raw;
    write4BE(raw, 8);
    raw.push_back('s');
    raw.push_back('i');
    raw.push_back('d');
    raw.push_back('x');

    auto buf = makeBuffer(raw);
    auto box = Box::createBox(*buf);
    ASSERT_NE(box, nullptr);
    EXPECT_EQ(box->info.symbol, "sidx");
    EXPECT_NE(std::dynamic_pointer_cast<BoxSidx>(box), nullptr);
}
