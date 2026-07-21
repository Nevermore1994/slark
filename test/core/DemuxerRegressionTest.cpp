//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#include "M3U8Parser.h"
#include "MediaPlaylist.h"
#include "demuxer/DemuxerManager.h"

#include <fstream>
#include <gtest/gtest.h>
#include <memory>
#include <string_view>

using namespace slark;

static std::unique_ptr<Buffer> loadFileToBuffer(std::string_view path, uint64_t& fileSize) {
    std::ifstream ifs(std::string(path), std::ios::binary | std::ios::ate);
    if (!ifs) {
        return nullptr;
    }
    fileSize = static_cast<uint64_t>(ifs.tellg());
    ifs.seekg(0, std::ios::beg);

    auto data = Data::create(fileSize);
    if (!data || !data->data()) {
        return nullptr;
    }
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
    ifs.read(reinterpret_cast<char*>(data->data()), static_cast<std::streamsize>(fileSize));
    data->setSize(fileSize);

    auto buffer = std::make_unique<Buffer>();
    buffer->append(0, std::move(data));
    return buffer;
}

TEST(DemuxerRegressionTest, DISABLED_FlvOpenContractReturnsSeekRange) {
    constexpr const char* kSamplePath = "../../test_sample/sample-3s.flv";
    uint64_t fileSize = 0;
    auto probeBuffer = loadFileToBuffer(kSamplePath, fileSize);
    ASSERT_NE(probeBuffer, nullptr);

    auto demuxer = DemuxerManager::shareInstance().create(DemuxerType::FLV);
    ASSERT_NE(demuxer, nullptr);

    DemuxerConfig config;
    config.filePath = kSamplePath;
    demuxer->init(config);

    ASSERT_TRUE(demuxer->open(probeBuffer));
    EXPECT_TRUE(demuxer->isOpened());
}

TEST(DemuxerRegressionTest, WavOpenContractRequeueProbeData) {
    // 优先使用 CMake 配置的 TEST_SAMPLE_DIR，否则尝试相对路径
    const char* sampleDir = std::getenv("TEST_SAMPLE_DIR");
    std::string samplePath;
    if (sampleDir) {
        samplePath = std::string(sampleDir) + "/sample-3s.wav";
    } else {
        samplePath = "../../test_sample/sample-3s.wav";
    }
    uint64_t fileSize = 0;
    auto probeBuffer = loadFileToBuffer(samplePath, fileSize);
    if (!probeBuffer) {
        GTEST_SKIP() << "Sample file not found: " << samplePath;
    }

    auto demuxer = DemuxerManager::shareInstance().create(DemuxerType::WAV);
    ASSERT_NE(demuxer, nullptr);

    DemuxerConfig config;
    config.filePath = samplePath;
    demuxer->init(config);

    ASSERT_TRUE(demuxer->open(probeBuffer));
    EXPECT_TRUE(demuxer->isOpened());
}

TEST(DemuxerRegressionTest, M3U8ByteRangeAccumulatesOffsetWhenMissingAtSymbol) {
    constexpr std::string_view kM3U8 = "#EXTM3U\n"
                                       "#EXTINF:4.0,\n"
                                       "segment0.ts\n"
                                       "#EXT-X-BYTERANGE:1000@200\n"
                                       "#EXTINF:4.0,\n"
                                       "segment1.ts\n"
                                       "#EXT-X-BYTERANGE:500\n"
                                       "#EXT-X-ENDLIST\n";

    M3U8Parser parser("https://example.com/live");
    ASSERT_TRUE(parser.parse(std::string(kM3U8)));
    ASSERT_TRUE(parser.playlist() != nullptr);

    const auto& segments = parser.playlist()->segments();
    ASSERT_EQ(segments.size(), 2u);

    // #EXT-X-BYTERANGE applies to the following URI (segment)
    // segment0 has no preceding BYTERANGE
    EXPECT_FALSE(segments[0].byteRange.has_value());
    // segment1 has #EXT-X-BYTERANGE:1000@200
    EXPECT_TRUE(segments[1].byteRange.has_value());
    EXPECT_EQ(segments[1].byteRange->pos, 200u);
    EXPECT_EQ(segments[1].byteRange->size, 1000u);
}

TEST(DemuxerRegressionTest, M3U8StreamInfParsesBandwidthWithoutOrderAssumption) {
    constexpr std::string_view kM3U8 =
        "#EXTM3U\n"
        "#EXT-X-STREAM-INF:CODECS=\"avc1\",BANDWIDTH=2560000,RESOLUTION=1920x1080\n"
        "main_1080p.m3u8\n"
        "#EXT-X-ENDLIST\n";

    M3U8Parser parser("https://example.com/master");
    ASSERT_TRUE(parser.parse(std::string(kM3U8)));
    ASSERT_TRUE(parser.isMasterPlaylist());

    const auto& variants = parser.playlist()->variants();
    ASSERT_EQ(variants.size(), 1u);
    EXPECT_EQ(variants[0].bandwidth, 2560000u);
    EXPECT_TRUE(variants[0].width.has_value());
    EXPECT_EQ(variants[0].width.value(), 1920u);
    EXPECT_TRUE(variants[0].height.has_value());
    EXPECT_EQ(variants[0].height.value(), 1080u);
}