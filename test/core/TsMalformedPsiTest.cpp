//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//
#include <gtest/gtest.h>

#include <cstdint>
#include <memory>

#include "AudioInfo.h"
#include "Mp4TestBytes.h"
#include "VideoInfo.h"
#include "demuxer/HLSDemuxer.h"

using namespace slark;
using slark::test::Bytes;
using slark::test::concat;
using slark::test::makeBuffer;

namespace {

constexpr size_t kTsPacketSize = 188;
constexpr uint16_t kPmtPid = 0x1000;

// payload is placed right after the 4-byte header, or after an adaptation field of
// adaptationLength bytes when it is non-negative.
Bytes tsPacket(uint16_t pid, bool payloadStart, const Bytes& payload, int adaptationLength = -1) {
    Bytes packet;
    packet.push_back(0x47);
    packet.push_back(static_cast<uint8_t>((payloadStart ? 0x40 : 0x00) | (pid >> 8)));
    packet.push_back(static_cast<uint8_t>(pid & 0xFF));
    if (adaptationLength >= 0) {
        packet.push_back(0x30);
        packet.push_back(static_cast<uint8_t>(adaptationLength));
        packet.resize(packet.size() + static_cast<size_t>(adaptationLength), 0xFF);
    } else {
        packet.push_back(0x10);
    }
    packet.insert(packet.end(), payload.begin(), payload.end());
    packet.resize(kTsPacketSize, 0xFF);
    return packet;
}

Bytes patSection(uint16_t sectionLength) {
    Bytes section = {0x00};  // pointer field
    section.push_back(0x00);  // table id
    section.push_back(static_cast<uint8_t>(0xB0 | (sectionLength >> 8)));
    section.push_back(static_cast<uint8_t>(sectionLength & 0xFF));
    section.insert(section.end(), {0x00, 0x01, 0xC1, 0x00, 0x00});
    section.insert(section.end(), {0x00, 0x01, static_cast<uint8_t>(0xE0 | (kPmtPid >> 8)),
                                   static_cast<uint8_t>(kPmtPid & 0xFF)});
    return section;
}

Bytes pmtSection(uint16_t sectionLength, uint16_t esInfoLength, bool withStreams) {
    Bytes section = {0x00};  // pointer field
    section.push_back(0x02);
    section.push_back(static_cast<uint8_t>(0xB0 | (sectionLength >> 8)));
    section.push_back(static_cast<uint8_t>(sectionLength & 0xFF));
    section.insert(section.end(), {0x00, 0x01, 0xC1, 0x00, 0x00});
    section.insert(section.end(), {0xE1, 0x00});  // PCR pid
    section.insert(section.end(), {0xF0, 0x00});  // program info length
    if (withStreams) {
        const auto esHigh = static_cast<uint8_t>(0xF0 | (esInfoLength >> 8));
        const auto esLow = static_cast<uint8_t>(esInfoLength & 0xFF);
        section.insert(section.end(), {0x1B, 0xE1, 0x00, esHigh, esLow});  // H.264
        section.insert(section.end(), {0x0F, 0xE1, 0x01, 0xF0, 0x00});     // AAC
    }
    return section;
}

struct Parsed {
    std::shared_ptr<AudioInfo> audio;
    std::shared_ptr<VideoInfo> video;
    bool ok = false;
};

Parsed feed(const Bytes& stream) {
    Parsed parsed;
    TSDemuxer demuxer(parsed.audio, parsed.video);
    auto buffer = makeBuffer(stream);
    DemuxerResult result;
    parsed.ok = demuxer.parseData(*buffer, 0, result);
    return parsed;
}

}  // namespace

TEST(TsMalformedPsiTest, ValidPatAndPmtDiscoverStreams) {
    auto parsed = feed(concat({tsPacket(0, true, patSection(13)),
                               tsPacket(kPmtPid, true, pmtSection(23, 0, true))}));
    EXPECT_TRUE(parsed.ok);
    ASSERT_NE(parsed.video, nullptr);
    EXPECT_EQ(parsed.video->mediaInfo, MEDIA_MIMETYPE_VIDEO_AVC);
    ASSERT_NE(parsed.audio, nullptr);
    EXPECT_EQ(parsed.audio->mediaInfo, MEDIA_MIMETYPE_AUDIO_AAC);
}

TEST(TsMalformedPsiTest, PatSectionLengthLargerThanPacketDoesNotOverread) {
    auto parsed = feed(tsPacket(0, true, patSection(4095)));
    EXPECT_TRUE(parsed.ok);
}

TEST(TsMalformedPsiTest, PatWithoutPayloadDoesNotOverread) {
    // The adaptation field swallows the whole payload, leaving an empty PSI view
    auto parsed = feed(tsPacket(0, true, {}, 183));
    EXPECT_TRUE(parsed.ok);
}

TEST(TsMalformedPsiTest, PatShorterThanFixedHeaderDoesNotOverread) {
    auto parsed = feed(tsPacket(0, true, {0x00, 0x00, 0xB0}, 178));
    EXPECT_TRUE(parsed.ok);
}

TEST(TsMalformedPsiTest, PmtSectionLengthLargerThanPacketDoesNotOverread) {
    auto parsed = feed(concat({tsPacket(0, true, patSection(13)),
                               tsPacket(kPmtPid, true, pmtSection(4095, 0, true))}));
    EXPECT_TRUE(parsed.ok);
}

TEST(TsMalformedPsiTest, PmtEsInfoLengthBeyondPacketDoesNotOverread) {
    auto parsed = feed(concat({tsPacket(0, true, patSection(13)),
                               tsPacket(kPmtPid, true, pmtSection(4095, 4095, true))}));
    EXPECT_TRUE(parsed.ok);
}

TEST(TsMalformedPsiTest, PmtWithoutPayloadDoesNotOverread) {
    auto parsed = feed(concat({tsPacket(0, true, patSection(13)),
                               tsPacket(kPmtPid, true, {}, 183)}));
    EXPECT_TRUE(parsed.ok);
}

TEST(TsMalformedPsiTest, PmtShorterThanFixedHeaderDoesNotOverread) {
    auto parsed = feed(concat({tsPacket(0, true, patSection(13)),
                               tsPacket(kPmtPid, true, {0x00, 0x02, 0xB0, 0x20}, 175)}));
    EXPECT_TRUE(parsed.ok);
}
