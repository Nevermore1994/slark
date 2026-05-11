#include "Buffer.hpp"
#include "DemuxerManager.h"
#include "File.h"
#include "IDemuxer.h"
#include "M3U8Parser.h"

#include <gtest/gtest.h>

using namespace slark;

namespace {

std::unique_ptr<Buffer> loadFileToBuffer(const std::string& filePath, uint64_t& fileSize) {
    File::ReadFile reader(filePath);
    auto openResult = reader.open();
    if (!openResult.has_value()) {
        return nullptr;
    }

    fileSize = reader.fileSize();
    auto readResult = reader.read(fileSize);
    if (!readResult.has_value()) {
        reader.close();
        return nullptr;
    }

    auto buffer = std::make_unique<Buffer>();
    auto data = std::make_unique<Data>(std::move(readResult.value()));
    if (!buffer->append(0, std::move(data))) {
        reader.close();
        return nullptr;
    }

    reader.close();
    return buffer;
}

} // namespace

TEST(DemuxerRegressionTest, Mp4OpenContractProvidesSeekRange) {
    constexpr const char* kSamplePath = "../../test_sample/Sample.mp4";
    uint64_t fileSize = 0;
    auto probeBuffer = loadFileToBuffer(kSamplePath, fileSize);
    ASSERT_NE(probeBuffer, nullptr);

    auto demuxer = DemuxerManager::shareInstance().create(DemuxerType::MP4);
    ASSERT_NE(demuxer, nullptr);

    DemuxerConfig config;
    config.filePath = kSamplePath;
    config.fileSize = fileSize;
    demuxer->init(config);

    ASSERT_TRUE(demuxer->open(probeBuffer));
    auto openResult = demuxer->onOpened();
    EXPECT_TRUE(openResult.success);
    EXPECT_TRUE(openResult.seekRange.has_value());
    EXPECT_GE(openResult.seekRange->start(), 0u);
}

TEST(DemuxerRegressionTest, WavOpenContractRequeueProbeData) {
    constexpr const char* kSamplePath = "../../test_sample/sample-3s.wav";
    uint64_t fileSize = 0;
    auto probeBuffer = loadFileToBuffer(kSamplePath, fileSize);
    ASSERT_NE(probeBuffer, nullptr);

    auto demuxer = DemuxerManager::shareInstance().create(DemuxerType::WAV);
    ASSERT_NE(demuxer, nullptr);

    DemuxerConfig config;
    config.filePath = kSamplePath;
    config.fileSize = fileSize;
    demuxer->init(config);

    ASSERT_TRUE(demuxer->open(probeBuffer));
    auto openResult = demuxer->onOpened();
    EXPECT_TRUE(openResult.success);
    EXPECT_TRUE(openResult.requeueProbeData);
    EXPECT_FALSE(openResult.seekRange.has_value());
}

TEST(DemuxerRegressionTest, M3U8ByteRangeAccumulatesOffsetWhenMissingAtSymbol) {
    constexpr std::string_view kM3U8 = "#EXTM3U\n"
                                       "#EXTINF:4.0,\n"
                                       "segment.ts\n"
                                       "#EXT-X-BYTERANGE:1000@200\n"
                                       "#EXTINF:4.0,\n"
                                       "segment.ts\n"
                                       "#EXT-X-BYTERANGE:500\n"
                                       "#EXT-X-ENDLIST\n";

    auto data = std::make_unique<Data>(kM3U8);
    Buffer buffer;
    ASSERT_TRUE(buffer.append(0, std::move(data)));

    M3U8Parser parser("https://example.com/live");
    ASSERT_TRUE(parser.parse(buffer));

    const auto& infos = parser.tsInfos();
    ASSERT_EQ(infos.size(), 2u);

    EXPECT_TRUE(infos[0].isSupportByteRange);
    EXPECT_EQ(infos[0].range.pos, 200u);
    EXPECT_EQ(infos[0].range.size, 1000);

    EXPECT_TRUE(infos[1].isSupportByteRange);
    EXPECT_EQ(infos[1].range.pos, 1200u);
    EXPECT_EQ(infos[1].range.size, 500);
}

TEST(DemuxerRegressionTest, M3U8StreamInfParsesBandwidthWithoutOrderAssumption) {
    constexpr std::string_view kM3U8 =
        "#EXTM3U\n"
        "#EXT-X-STREAM-INF:CODECS=\"avc1\",BANDWIDTH=2560000,RESOLUTION=1920x1080\n"
        "main_1080p.m3u8\n"
        "#EXT-X-ENDLIST\n";

    auto data = std::make_unique<Data>(kM3U8);
    Buffer buffer;
    ASSERT_TRUE(buffer.append(0, std::move(data)));

    M3U8Parser parser("https://example.com/master");
    ASSERT_TRUE(parser.parse(buffer));
    ASSERT_TRUE(parser.isPlayList());

    const auto& playlists = parser.playListInfos();
    ASSERT_EQ(playlists.size(), 1u);
    EXPECT_EQ(playlists[0].codeRate, 2560000u);
    EXPECT_EQ(playlists[0].m3u8Url, "https://example.com/master/main_1080p.m3u8");
}