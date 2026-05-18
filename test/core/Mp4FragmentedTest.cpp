//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#include "Buffer.hpp"
#include "demuxer/Mp4Box.h"
#include "demuxer/Mp4Demuxer.h"

#include <gtest/gtest.h>
#include <vector>

using namespace slark;

namespace {

// 构造一个最小的 Buffer，写入大端字节序数据
std::unique_ptr<Buffer> makeBuffer(std::vector<uint8_t> bytes) {
    auto data = Data::create(bytes.size());
    std::memcpy(data->data(), bytes.data(), bytes.size());
    data->setSize(bytes.size());
    auto buf = std::make_unique<Buffer>();
    buf->append(0, std::move(data));
    return buf;
}

// 写入 4 字节大端
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

} // namespace

// ─── BoxElst 解析测试 ─────────────────────────────────────────────────────────

TEST(BoxElstTest, DecodeV0SingleEntry) {
    // version=0, flags=0, entryCount=1
    // segmentDuration=3000, mediaTime=512, mediaRateInteger=1, mediaRateFraction=0
    std::vector<uint8_t> data;
    data.push_back(0); // version
    data.push_back(0);
    data.push_back(0);
    data.push_back(0);    // flags
    write4BE(data, 1);    // entryCount
    write4BE(data, 3000); // segmentDuration
    write4BE(data, 512);  // mediaTime
    write2BE(data, 1);    // mediaRateInteger
    write2BE(data, 0);    // mediaRateFraction

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
    // initial empty edit: mediaTime = -1
    std::vector<uint8_t> data;
    data.push_back(0);
    data.push_back(0);
    data.push_back(0);
    data.push_back(0);
    write4BE(data, 2); // entryCount=2
    // entry 0: empty edit
    write4BE(data, 1000);
    write4BE(data, static_cast<uint32_t>(-1)); // mediaTime = -1
    write2BE(data, 1);
    write2BE(data, 0);
    // entry 1: real edit
    write4BE(data, 5000);
    write4BE(data, 0); // mediaTime = 0
    write2BE(data, 1);
    write2BE(data, 0);

    BoxInfo info;
    info.size = data.size() + 8;
    BoxElst box(std::move(info));
    auto buf = makeBuffer(data);
    ASSERT_TRUE(box.decode(*buf));
    ASSERT_EQ(box.entries.size(), 2u);
    EXPECT_TRUE(box.hasEmptyEdit());
    EXPECT_EQ(box.mediaTimeOffset(), 0); // 跳过 empty edit，取第二个 entry 的 mediaTime=0
}

TEST(BoxElstTest, DecodeV1Entry) {
    // version=1: 8-byte segmentDuration and mediaTime
    std::vector<uint8_t> data;
    data.push_back(1); // version
    data.push_back(0);
    data.push_back(0);
    data.push_back(0);
    write4BE(data, 1);        // entryCount
    write8BE(data, 90000ULL); // segmentDuration
    write8BE(data, 1024ULL);  // mediaTime
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

// ─── BoxTrex 解析测试 ─────────────────────────────────────────────────────────

TEST(BoxTrexTest, Decode) {
    std::vector<uint8_t> data;
    data.push_back(0); // version
    data.push_back(0);
    data.push_back(0);
    data.push_back(0);    // flags
    write4BE(data, 1);    // trackId
    write4BE(data, 1);    // defaultSampleDescriptionIndex
    write4BE(data, 512);  // defaultSampleDuration
    write4BE(data, 1024); // defaultSampleSize
    write4BE(data, 0);    // defaultSampleFlags

    BoxInfo info;
    info.size = data.size() + 8;
    BoxTrex box(std::move(info));
    auto buf = makeBuffer(data);
    ASSERT_TRUE(box.decode(*buf));
    EXPECT_EQ(box.trackId, 1u);
    EXPECT_EQ(box.defaultSampleDuration, 512u);
    EXPECT_EQ(box.defaultSampleSize, 1024u);
}

// ─── BoxTfhd 解析测试 ─────────────────────────────────────────────────────────

TEST(BoxTfhdTest, DecodeMinimal) {
    std::vector<uint8_t> data;
    data.push_back(0); // version
    data.push_back(0);
    data.push_back(0);
    data.push_back(0); // flags = 0
    write4BE(data, 2); // trackId

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
    data.push_back(0); // version
    // flags: kDefaultSampleDurationPresent = 0x000008
    data.push_back(0);
    data.push_back(0);
    data.push_back(0x08);
    write4BE(data, 1);    // trackId
    write4BE(data, 3000); // defaultSampleDuration

    BoxInfo info;
    info.size = data.size() + 8;
    BoxTfhd box(std::move(info));
    auto buf = makeBuffer(data);
    ASSERT_TRUE(box.decode(*buf));
    EXPECT_EQ(box.trackId, 1u);
    EXPECT_EQ(box.defaultSampleDuration, 3000u);
}

// ─── BoxTfdt 解析测试 ─────────────────────────────────────────────────────────

TEST(BoxTfdtTest, DecodeV0) {
    std::vector<uint8_t> data;
    data.push_back(0); // version
    data.push_back(0);
    data.push_back(0);
    data.push_back(0);
    write4BE(data, 90000); // baseMediaDecodeTime

    BoxInfo info;
    info.size = data.size() + 8;
    BoxTfdt box(std::move(info));
    auto buf = makeBuffer(data);
    ASSERT_TRUE(box.decode(*buf));
    EXPECT_EQ(box.baseMediaDecodeTime, 90000u);
}

TEST(BoxTfdtTest, DecodeV1) {
    std::vector<uint8_t> data;
    data.push_back(1); // version
    data.push_back(0);
    data.push_back(0);
    data.push_back(0);
    write8BE(data, 9000000000ULL); // large baseMediaDecodeTime

    BoxInfo info;
    info.size = data.size() + 8;
    BoxTfdt box(std::move(info));
    auto buf = makeBuffer(data);
    ASSERT_TRUE(box.decode(*buf));
    EXPECT_EQ(box.baseMediaDecodeTime, 9000000000ULL);
}

// ─── BoxTrun 解析测试 ─────────────────────────────────────────────────────────

TEST(BoxTrunTest, DecodeWithSizeAndDuration) {
    // flags: kDataOffsetPresent | kSampleDurationPresent | kSampleSizePresent
    constexpr uint32_t kFlags =
        BoxTrun::kDataOffsetPresent | BoxTrun::kSampleDurationPresent | BoxTrun::kSampleSizePresent;
    std::vector<uint8_t> data;
    data.push_back(0); // version
    data.push_back(static_cast<uint8_t>(kFlags >> 16));
    data.push_back(static_cast<uint8_t>(kFlags >> 8));
    data.push_back(static_cast<uint8_t>(kFlags));
    write4BE(data, 3); // sampleCount = 3
    write4BE(data, 8); // dataOffset
    // 3 samples
    for (int i = 0; i < 3; ++i) {
        write4BE(data, 512);  // duration
        write4BE(data, 1024); // size
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

// ─── BoxSidx 解析测试 ─────────────────────────────────────────────────────────

TEST(BoxSidxTest, DecodeV0) {
    std::vector<uint8_t> data;
    data.push_back(0); // version
    data.push_back(0);
    data.push_back(0);
    data.push_back(0);
    write4BE(data, 1);     // referenceId
    write4BE(data, 90000); // timeScale
    write4BE(data, 0);     // earliestPresentationTime
    write4BE(data, 0);     // firstOffset
    write2BE(data, 0);     // reserved
    write2BE(data, 2);     // entryCount = 2
    // entry 0: media segment, size=1000, duration=9000, startsWithSap=1, sapType=1
    write4BE(data, 1000);                    // referenceType=0, referencedSize=1000
    write4BE(data, 9000);                    // subsegmentDuration
    write4BE(data, (1u << 31) | (1u << 28)); // startsWithSap=1, sapType=1
    // entry 1
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

// ─── BoxMfhd 解析测试 ─────────────────────────────────────────────────────────

TEST(BoxMfhdTest, Decode) {
    std::vector<uint8_t> data;
    data.push_back(0);
    data.push_back(0);
    data.push_back(0);
    data.push_back(0);
    write4BE(data, 42); // sequenceNumber

    BoxInfo info;
    info.size = data.size() + 8;
    BoxMfhd box(std::move(info));
    auto buf = makeBuffer(data);
    ASSERT_TRUE(box.decode(*buf));
    EXPECT_EQ(box.sequenceNumber, 42u);
}

// ─── Box::createBox 工厂测试 ──────────────────────────────────────────────────

TEST(BoxFactoryTest, CreateElstBox) {
    // 构造一个完整的 elst box（size=8+header, type='elst'）
    std::vector<uint8_t> raw;
    write4BE(raw,
             8 + 4 + 4 + 12); // size: header(8) + version/flags(4) + entryCount(4) + 1 entry(12)
    raw.push_back('e');
    raw.push_back('l');
    raw.push_back('s');
    raw.push_back('t');
    // elst data
    raw.push_back(0);
    raw.push_back(0);
    raw.push_back(0);
    raw.push_back(0); // version+flags
    write4BE(raw, 1); // entryCount
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
    write4BE(raw, 8); // size
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
