//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#include "MediaUtil.h"

#include <gtest/gtest.h>

#include <vector>

#include "AudioInfo.h"
#include "DataView.hpp"
#include "Range.h"
#include "VideoInfo.h"

using namespace slark;

namespace {

// Minimal MSB-first bit writer for building SPS RBSP test streams.
class BitWriter {
 public:
    void bit(uint32_t v) { bits_.push_back(v ? '1' : '0'); }

    void bits(uint32_t value, int count) {
        for (int i = count - 1; i >= 0; --i) {
            bit((value >> i) & 1u);
        }
    }

    void ue(uint32_t codeNum) {
        const uint32_t v = codeNum + 1;
        int len = 0;
        for (uint32_t t = v; t > 0; t >>= 1) {
            ++len;
        }
        for (int i = 0; i < len - 1; ++i) {
            bit(0);
        }
        bits(v, len);
    }

    [[nodiscard]] std::vector<uint8_t> bytes() const {
        std::vector<uint8_t> out;
        for (size_t i = 0; i < bits_.size(); i += 8) {
            uint8_t b = 0;
            for (size_t j = 0; j < 8 && i + j < bits_.size(); ++j) {
                b = static_cast<uint8_t>((b << 1) | (bits_[i + j] == '1' ? 1 : 0));
            }
            out.push_back(b);
        }
        return out;
    }

 private:
    std::string bits_;
};

// Builds an H.264 SPS NAL unit. baseline profile keeps the scaling-matrix block out.
[[nodiscard]] std::vector<uint8_t> makeH264Spsnal(uint32_t numRefFrames,
                                                  uint32_t maxNumReorderFrames,
                                                  bool withBitstreamRestriction) {
    BitWriter w;
    w.bits(66, 8);  // profile_idc
    w.bits(0, 8);   // constraint flags + reserved
    w.bits(30, 8);  // level_idc
    w.ue(0);        // seq_parameter_set_id
    w.ue(0);        // log2_max_frame_num_minus4
    w.ue(0);        // pic_order_cnt_type
    w.ue(0);        // log2_max_pic_order_cnt_lsb_minus4
    w.ue(numRefFrames);
    w.bit(0);  // gaps_in_frame_num_value_allowed_flag
    w.ue(0);   // pic_width_in_mbs_minus1
    w.ue(0);   // pic_height_in_map_units_minus1
    w.bit(1);  // frame_mbs_only_flag
    w.bit(1);  // direct_8x8_inference_flag
    w.bit(0);  // frame_cropping_flag
    w.bit(1);  // vui_parameters_present_flag

    // VUI: optional blocks disabled up to bitstream_restriction.
    w.bit(0);  // aspect_ratio_info_present_flag
    w.bit(0);  // overscan_info_present_flag
    w.bit(0);  // video_signal_type_present_flag
    w.bit(0);  // chroma_loc_info_present_flag
    w.bit(0);  // timing_info_present_flag
    w.bit(0);  // nal_hrd_parameters_present_flag
    w.bit(0);  // vcl_hrd_parameters_present_flag
    w.bit(0);  // pic_struct_present_flag

    w.bit(withBitstreamRestriction ? 1u : 0u);  // bitstream_restriction_flag
    if (withBitstreamRestriction) {
        w.bit(1);  // motion_vectors_over_pic_boundaries_flag
        w.ue(0);   // max_bytes_per_pic_denom
        w.ue(0);   // max_bits_per_mb_denom
        w.ue(0);   // log2_max_mv_length_horizontal
        w.ue(0);   // log2_max_mv_length_vertical
        w.ue(maxNumReorderFrames);
        w.ue(0);  // max_dec_frame_buffering
    }

    auto rbsp = w.bytes();
    std::vector<uint8_t> nalu;
    nalu.push_back(0x67);  // NAL header: SPS
    nalu.insert(nalu.end(), rbsp.begin(), rbsp.end());
    return nalu;
}

}  // namespace


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


TEST(MediaUtilTest, ParsesMaxNumReorderFramesFromSpsVui) {
    auto data = makeH264Spsnal(2, 3, true);
    DataView view(reinterpret_cast<const Byte*>(data.data()), data.size());

    auto info = std::make_shared<VideoInfo>();
    parseH264Sps(view, info);

    EXPECT_EQ(info->numRefFrames, 2);
    EXPECT_EQ(info->maxNumReorderFrames, 3);
}

TEST(MediaUtilTest, MaxNumReorderFramesUnknownWithoutBitstreamRestriction) {
    // Without bitstream_restriction_flag the reorder depth is simply not signalled.
    auto data = makeH264Spsnal(2, 3, false);
    DataView view(reinterpret_cast<const Byte*>(data.data()), data.size());

    auto info = std::make_shared<VideoInfo>();
    parseH264Sps(view, info);

    EXPECT_EQ(info->numRefFrames, 2);
    EXPECT_EQ(info->maxNumReorderFrames, 0);
}

TEST(MediaUtilTest, MaxNumReorderFramesIsClamped) {
    // Absurd reorder depths must not blow up the decode backlog.
    auto data = makeH264Spsnal(2, 200, true);
    DataView view(reinterpret_cast<const Byte*>(data.data()), data.size());

    auto info = std::make_shared<VideoInfo>();
    parseH264Sps(view, info);

    EXPECT_LE(info->maxNumReorderFrames, 16);
    EXPECT_GT(info->maxNumReorderFrames, 0);
}

TEST(MediaUtilTest, FindNaluUnitAnnexB4ByteStartCode) {
    std::vector<uint8_t> data = {0x00, 0x00, 0x00, 0x01, 0x65, 0xAA, 0xBB,
                                 0x00, 0x00, 0x00, 0x01, 0x41, 0xCC};
    DataView view(reinterpret_cast<const Byte*>(data.data()), data.size());
    Range range;
    EXPECT_TRUE(findNaluUnit(view, range));
    EXPECT_EQ(range.size, 3u);
}

TEST(MediaUtilTest, FindNaluUnitAnnexB3ByteStartCode) {
    std::vector<uint8_t> data = {0x00, 0x00, 0x01, 0x65, 0xAA, 0x00, 0x00, 0x01, 0x41};
    DataView view(reinterpret_cast<const Byte*>(data.data()), data.size());
    Range range;
    EXPECT_TRUE(findNaluUnit(view, range));
    EXPECT_EQ(range.size, 2u);
}

TEST(MediaUtilTest, FindNaluUnitNoStartCodeReturnsFalse) {
    std::vector<uint8_t> data = {0x65, 0xAA, 0xBB, 0xCC};
    DataView view(reinterpret_cast<const Byte*>(data.data()), data.size());
    Range range;
    EXPECT_FALSE(findNaluUnit(view, range));
}

TEST(MediaUtilTest, FindNaluUnitSingleNaluToEnd) {
    std::vector<uint8_t> data = {0x00, 0x00, 0x00, 0x01, 0x65, 0xAA, 0xBB, 0xCC};
    DataView view(reinterpret_cast<const Byte*>(data.data()), data.size());
    Range range;
    EXPECT_TRUE(findNaluUnit(view, range));
    EXPECT_EQ(range.size, 4u);
}

TEST(MediaUtilTest, FindNaluUnitEmptyDataReturnsFalse) {
    DataView view;
    Range range;
    EXPECT_FALSE(findNaluUnit(view, range));
}


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
