//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#include "MediaUtil.h"

#include <gtest/gtest.h>

#include <vector>

#include "AudioInfo.h"
#include "DataView.hpp"
#include "Range.h"

using namespace slark;

// MARK: - isNetworkLink

TEST(MediaUtilTest, IsNetworkLinkHttp) {
    EXPECT_TRUE(isNetworkLink("http://example.com/video.mp4"));
    EXPECT_TRUE(isNetworkLink("https://cdn.example.com/live/stream.m3u8"));
    EXPECT_TRUE(isNetworkLink("http://192.168.1.1:8080/video"));
}

TEST(MediaUtilTest, IsNetworkLinkLocalPath) {
    EXPECT_FALSE(isNetworkLink("/sdcard/video.mp4"));
    EXPECT_FALSE(isNetworkLink("file:///data/local/video.mp4"));
    EXPECT_FALSE(isNetworkLink("rtmp://stream.example.com/live"));
    EXPECT_FALSE(isNetworkLink(""));
}

TEST(MediaUtilTest, IsNetworkLinkEdgeCases) {
    EXPECT_FALSE(isNetworkLink("ftp://example.com/video.mp4"));
    EXPECT_FALSE(isNetworkLink("httpx://example.com/video.mp4"));
    EXPECT_TRUE(isNetworkLink("https://a.b"));
}

// MARK: - isHlsLink

TEST(MediaUtilTest, IsHlsLinkValidM3U8) {
    EXPECT_TRUE(isHlsLink("http://example.com/stream.m3u8"));
    EXPECT_TRUE(isHlsLink("https://cdn.example.com/live/index.m3u8"));
    EXPECT_TRUE(isHlsLink("https://example.com/video.M3U8"));
    EXPECT_TRUE(isHlsLink("http://example.com/stream.m3u8?token=abc&ts=123"));
}

TEST(MediaUtilTest, IsHlsLinkNonM3U8) {
    EXPECT_FALSE(isHlsLink("http://example.com/video.mp4"));
    EXPECT_FALSE(isHlsLink("https://example.com/stream.ts"));
    EXPECT_FALSE(isHlsLink("/local/path/stream.m3u8"));
    EXPECT_FALSE(isHlsLink(""));
}

TEST(MediaUtilTest, IsHlsLinkEdgeCases) {
    EXPECT_FALSE(isHlsLink("http://example.com/m3u8"));
    EXPECT_FALSE(isHlsLink("http://example.com/stream.m3u8.bak"));
    EXPECT_TRUE(isHlsLink("https://example.com/a.m3u8?x=1"));
}

// MARK: - getAACSamplingRate

TEST(MediaUtilTest, GetAACSamplingRateKnownIndices) {
    EXPECT_EQ(getAACSamplingRate(0), 96000);
    EXPECT_EQ(getAACSamplingRate(1), 88200);
    EXPECT_EQ(getAACSamplingRate(2), 64000);
    EXPECT_EQ(getAACSamplingRate(3), 48000);
    EXPECT_EQ(getAACSamplingRate(4), 44100);
    EXPECT_EQ(getAACSamplingRate(5), 32000);
    EXPECT_EQ(getAACSamplingRate(6), 24000);
    EXPECT_EQ(getAACSamplingRate(7), 22050);
    EXPECT_EQ(getAACSamplingRate(8), 16000);
    EXPECT_EQ(getAACSamplingRate(9), 12000);
    EXPECT_EQ(getAACSamplingRate(10), 11025);
    EXPECT_EQ(getAACSamplingRate(11), 8000);
    EXPECT_EQ(getAACSamplingRate(12), 7350);
}

TEST(MediaUtilTest, GetAACSamplingRateUnknownIndexReturnsDefault) {
    EXPECT_EQ(getAACSamplingRate(13), 44100);
    EXPECT_EQ(getAACSamplingRate(99), 44100);
    EXPECT_EQ(getAACSamplingRate(-1), 44100);
}

// MARK: - getAudioProfile

TEST(MediaUtilTest, GetAudioProfileValidProfiles) {
    EXPECT_EQ(getAudioProfile(1), AudioProfile::AAC_MAIN);
    EXPECT_EQ(getAudioProfile(2), AudioProfile::AAC_LC);
    EXPECT_EQ(getAudioProfile(3), AudioProfile::AAC_SSR);
    EXPECT_EQ(getAudioProfile(4), AudioProfile::AAC_LTP);
    EXPECT_EQ(getAudioProfile(5), AudioProfile::AAC_HE);
    EXPECT_EQ(getAudioProfile(29), AudioProfile::AAC_HE_SP);
}

TEST(MediaUtilTest, GetAudioProfileInvalidReturnsAACLC) {
    EXPECT_EQ(getAudioProfile(0), AudioProfile::AAC_LC);
    EXPECT_EQ(getAudioProfile(6), AudioProfile::AAC_LC);
    EXPECT_EQ(getAudioProfile(100), AudioProfile::AAC_LC);
    EXPECT_EQ(getAudioProfile(255), AudioProfile::AAC_LC);
}

// MARK: - findNaluUnit (AnnexB 起始码解析)

TEST(MediaUtilTest, FindNaluUnitAnnexB4ByteStartCode) {
    // 00 00 00 01 [nalu data] 00 00 00 01 [next nalu]
    std::vector<uint8_t> data =
        {0x00, 0x00, 0x00, 0x01, 0x65, 0xAA, 0xBB, 0x00, 0x00, 0x00, 0x01, 0x41, 0xCC};
    DataView view(reinterpret_cast<const Byte*>(data.data()), data.size());
    Range range;
    EXPECT_TRUE(findNaluUnit(view, range));
    // 起始码后的 nalu 数据：65 AA BB，size 应为 3
    EXPECT_EQ(range.size, 3u);
}

TEST(MediaUtilTest, FindNaluUnitAnnexB3ByteStartCode) {
    // 00 00 01 [nalu data] 00 00 01 [next nalu]
    std::vector<uint8_t> data = {0x00, 0x00, 0x01, 0x65, 0xAA, 0x00, 0x00, 0x01, 0x41};
    DataView view(reinterpret_cast<const Byte*>(data.data()), data.size());
    Range range;
    EXPECT_TRUE(findNaluUnit(view, range));
    EXPECT_EQ(range.size, 2u);  // 65 AA
}

TEST(MediaUtilTest, FindNaluUnitNoStartCodeReturnsFalse) {
    std::vector<uint8_t> data = {0x65, 0xAA, 0xBB, 0xCC};
    DataView view(reinterpret_cast<const Byte*>(data.data()), data.size());
    Range range;
    EXPECT_FALSE(findNaluUnit(view, range));
}

TEST(MediaUtilTest, FindNaluUnitSingleNaluToEnd) {
    // 只有一个 NALU，没有下一个起始码
    std::vector<uint8_t> data = {0x00, 0x00, 0x00, 0x01, 0x65, 0xAA, 0xBB, 0xCC};
    DataView view(reinterpret_cast<const Byte*>(data.data()), data.size());
    Range range;
    EXPECT_TRUE(findNaluUnit(view, range));
    EXPECT_EQ(range.size, 4u);  // 65 AA BB CC
}

TEST(MediaUtilTest, FindNaluUnitEmptyDataReturnsFalse) {
    DataView view;
    Range range;
    EXPECT_FALSE(findNaluUnit(view, range));
}

// MARK: - MediaDefs 常量

TEST(MediaDefsTest, MimeTypeStringsAreCorrect) {
    EXPECT_EQ(MEDIA_MIMETYPE_VIDEO_AVC, "video/avc");
    EXPECT_EQ(MEDIA_MIMETYPE_VIDEO_HEVC, "video/hevc");
    EXPECT_EQ(MEDIA_MIMETYPE_AUDIO_AAC, "audio/acc");
    EXPECT_EQ(MEDIA_MIMETYPE_AUDIO_RAW, "audio/raw");
    EXPECT_EQ(MEDIA_MIMETYPE_UNKNOWN, "unknown");
}

TEST(MediaDefsTest, TrackTypeEnumValues) {
    EXPECT_EQ(static_cast<int32_t>(TrackType::Unknown), 0);
    EXPECT_EQ(static_cast<int32_t>(TrackType::Audio), 1);
    EXPECT_EQ(static_cast<int32_t>(TrackType::Video), 2);
}
