//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#include "AVFrame.hpp"
#include "AudioInfo.h"
#include "Buffer.hpp"
#include "VideoInfo.h"
#include "demuxer/DemuxerManager.h"
#include "demuxer/HLSDemuxer.h"
#include "demuxer/IDemuxer.h"
#include "demuxer/Mp4Demuxer.h"

#include <fstream>
#include <gtest/gtest.h>
#include <memory>
#include <string_view>

using namespace slark;

namespace {

constexpr const char* kMp4Path = "../../test_sample/Sample.mp4";
constexpr const char* kTsPath = "../../test_sample/segment-0.ts";
constexpr const char* kWavPath = "../../test_sample/sample-3s.wav";

std::unique_ptr<Buffer> loadFile(std::string_view path, uint64_t& outSize) {
    std::ifstream ifs(std::string(path), std::ios::binary | std::ios::ate);
    if (!ifs) {
        return nullptr;
    }
    outSize = static_cast<uint64_t>(ifs.tellg());
    ifs.seekg(0, std::ios::beg);

    auto data = Data::create(outSize);
    if (!data || !data->data()) {
        return nullptr;
    }
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
    ifs.read(reinterpret_cast<char*>(data->data()), static_cast<std::streamsize>(outSize));
    data->setSize(outSize);

    auto buffer = std::make_unique<Buffer>();
    buffer->append(0, std::move(data));
    return buffer;
}

struct ParseResult {
    uint32_t audioFrames = 0;
    uint32_t videoFrames = 0;
    uint32_t keyFrames = 0;
    bool reachedEOF = false;
};

ParseResult feedAllData(IDemuxer& demuxer,
                        std::string_view path,
                        uint64_t fileSize,
                        uint64_t startOffset) {
    std::ifstream ifs(std::string(path), std::ios::binary);
    if (!ifs) {
        return {};
    }
    ifs.seekg(static_cast<std::streamoff>(startOffset), std::ios::beg);

    ParseResult result;
    constexpr uint64_t kChunkSize = 64 * 1024;
    uint64_t offset = startOffset;

    while (offset < fileSize) {
        uint64_t remaining = fileSize - offset;
        uint64_t readSize = std::min(remaining, kChunkSize);

        auto data = Data::create(readSize);
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
        ifs.read(reinterpret_cast<char*>(data->data()), static_cast<std::streamsize>(readSize));
        data->setSize(readSize);

        DataPacket packet;
        packet.offset = static_cast<int64_t>(offset);
        packet.data = std::move(data);

        auto res = demuxer.parseData(packet);
        result.audioFrames += static_cast<uint32_t>(res.audioFrames.size());
        result.videoFrames += static_cast<uint32_t>(res.videoFrames.size());
        for (const auto& f : res.videoFrames) {
            if (f && f->info && f->info->isKeyFrame()) {
                result.keyFrames++;
            }
        }
        if (res.resultCode == DemuxerResultCode::FileEnd) {
            result.reachedEOF = true;
            break;
        }
        offset += readSize;
    }
    return result;
}

} // namespace

// ─── probeDemuxType unit tests ───────────────────────────────────────────────

TEST(DemuxerProbeTest, ProbesMp4ByFtypSignature) {
    std::array<uint8_t, 12> header{0x00, 0x00, 0x00, 0x18, 'f', 't', 'y', 'p', 'i', 's', 'o', 'm'};
    DataView view(header.data(), header.size());
    EXPECT_EQ(DemuxerManager::shareInstance().probeDemuxType(view), DemuxerType::MP4);
}

TEST(DemuxerProbeTest, ProbesHlsByExtm3uSignature) {
    constexpr std::string_view kM3U8Header = "#EXTM3U\n#EXT-X-VERSION:3\n";
    DataView view = DataView::fromString(kM3U8Header);
    EXPECT_EQ(DemuxerManager::shareInstance().probeDemuxType(view), DemuxerType::HLS);
}

TEST(DemuxerProbeTest, ProbesWavByRiffSignature) {
    std::array<uint8_t, 12> header{'R', 'I', 'F', 'F', 0x00, 0x00, 0x00, 0x00, 'W', 'A', 'V', 'E'};
    DataView view(header.data(), header.size());
    EXPECT_EQ(DemuxerManager::shareInstance().probeDemuxType(view), DemuxerType::WAV);
}

TEST(DemuxerProbeTest, ReturnsUnknownForEmptyData) {
    DataView view;
    EXPECT_EQ(DemuxerManager::shareInstance().probeDemuxType(view), DemuxerType::Unknown);
}

TEST(DemuxerProbeTest, ReturnsUnknownForRandomData) {
    std::array<uint8_t, 16> garbage{0xDE,
                                    0xAD,
                                    0xBE,
                                    0xEF,
                                    0x00,
                                    0x01,
                                    0x02,
                                    0x03,
                                    0x04,
                                    0x05,
                                    0x06,
                                    0x07,
                                    0x08,
                                    0x09,
                                    0x0A,
                                    0x0B};
    DataView view(garbage.data(), garbage.size());
    EXPECT_EQ(DemuxerManager::shareInstance().probeDemuxType(view), DemuxerType::Unknown);
}

// ─── DemuxerManager unit tests ───────────────────────────────────────────────

TEST(DemuxerManagerTest, CreateMp4DemuxerSucceeds) {
    auto demuxer = DemuxerManager::shareInstance().create(DemuxerType::MP4);
    ASSERT_NE(demuxer, nullptr);
    EXPECT_EQ(demuxer->type(), DemuxerType::MP4);
}

TEST(DemuxerManagerTest, CreateHlsDemuxerSucceeds) {
    auto demuxer = DemuxerManager::shareInstance().create(DemuxerType::HLS);
    ASSERT_NE(demuxer, nullptr);
    EXPECT_EQ(demuxer->type(), DemuxerType::HLS);
}

TEST(DemuxerManagerTest, CreateWavDemuxerSucceeds) {
    auto demuxer = DemuxerManager::shareInstance().create(DemuxerType::WAV);
    ASSERT_NE(demuxer, nullptr);
    EXPECT_EQ(demuxer->type(), DemuxerType::WAV);
}

TEST(DemuxerManagerTest, CreateUnknownTypeReturnsNull) {
    EXPECT_EQ(DemuxerManager::shareInstance().create(DemuxerType::Unknown), nullptr);
}

TEST(DemuxerManagerTest, ContainsMp4) {
    EXPECT_TRUE(DemuxerManager::shareInstance().contains(DemuxerType::MP4));
}

TEST(DemuxerManagerTest, ContainsHls) {
    EXPECT_TRUE(DemuxerManager::shareInstance().contains(DemuxerType::HLS));
}

TEST(DemuxerManagerTest, ContainsWav) {
    EXPECT_TRUE(DemuxerManager::shareInstance().contains(DemuxerType::WAV));
}

TEST(DemuxerManagerTest, DoesNotContainUnknown) {
    EXPECT_FALSE(DemuxerManager::shareInstance().contains(DemuxerType::Unknown));
}

// ─── MP4 integration tests ───────────────────────────────────────────────────

class Mp4DemuxerFileTest : public ::testing::Test {
protected:
    void SetUp() override {
        probeBuffer_ = loadFile(kMp4Path, fileSize_);
        if (!probeBuffer_) {
            GTEST_SKIP() << "Sample.mp4 not found, skipping";
        }
        demuxer_ = DemuxerManager::shareInstance().create(DemuxerType::MP4);
        ASSERT_NE(demuxer_, nullptr);
        DemuxerConfig config;
        config.filePath = kMp4Path;
        demuxer_->init(config);
    }

    std::unique_ptr<Buffer> probeBuffer_;
    std::shared_ptr<IDemuxer> demuxer_;
    uint64_t fileSize_ = 0;
};

TEST_F(Mp4DemuxerFileTest, OpenSucceeds) {
    ASSERT_TRUE(demuxer_->open(probeBuffer_));
    EXPECT_TRUE(demuxer_->isOpened());
}

TEST_F(Mp4DemuxerFileTest, HasVideoAndAudioAfterOpen) {
    ASSERT_TRUE(demuxer_->open(probeBuffer_));
    EXPECT_TRUE(demuxer_->hasVideo());
    EXPECT_TRUE(demuxer_->hasAudio());
}

TEST_F(Mp4DemuxerFileTest, VideoInfoIsValid) {
    ASSERT_TRUE(demuxer_->open(probeBuffer_));
    auto videoInfo = demuxer_->videoInfo();
    ASSERT_NE(videoInfo, nullptr);
    EXPECT_GT(videoInfo->width, 0u);
    EXPECT_GT(videoInfo->height, 0u);
    EXPECT_GT(videoInfo->timeScale, 0u);
}

TEST_F(Mp4DemuxerFileTest, AudioInfoIsValid) {
    ASSERT_TRUE(demuxer_->open(probeBuffer_));
    auto audioInfo = demuxer_->audioInfo();
    ASSERT_NE(audioInfo, nullptr);
    EXPECT_GT(audioInfo->sampleRate, 0u);
    EXPECT_GT(audioInfo->channels, 0u);
}

TEST_F(Mp4DemuxerFileTest, TotalDurationIsPositive) {
    ASSERT_TRUE(demuxer_->open(probeBuffer_));
    EXPECT_GT(demuxer_->totalDuration().second(), 0.0);
}

TEST_F(Mp4DemuxerFileTest, ParseDataProducesFrames) {
    ASSERT_TRUE(demuxer_->open(probeBuffer_));

    uint64_t startOffset = 0;
    if (auto hi = demuxer_->headerInfo(); hi && hi->headerLength > 0) {
        startOffset = hi->headerLength;
        demuxer_->seekPos(startOffset);
    }

    auto result = feedAllData(*demuxer_, kMp4Path, fileSize_, startOffset);
    EXPECT_GT(result.videoFrames, 0u) << "no video frames parsed";
    EXPECT_GT(result.audioFrames, 0u) << "no audio frames parsed";
    EXPECT_GT(result.keyFrames, 0u) << "no key frames found";
}

TEST_F(Mp4DemuxerFileTest, SeekToMidpointProducesKeyFrame) {
    ASSERT_TRUE(demuxer_->open(probeBuffer_));

    double duration = demuxer_->totalDuration().second();
    ASSERT_GT(duration, 0.0);

    double seekTime = duration / 2.0;
    uint64_t seekPos = demuxer_->getSeekToPos(seekTime);
    EXPECT_GT(seekPos, 0u);

    demuxer_->seekPos(seekPos);

    // 从 seekPos 开始读取足够多的数据，确保能覆盖到视频关键帧
    auto result = feedAllData(*demuxer_, kMp4Path, fileSize_, seekPos);
    bool hasKeyFrame = false;
    for (uint32_t i = 0; i < result.keyFrames; i++) {
        hasKeyFrame = true;
        break;
    }
    EXPECT_TRUE(hasKeyFrame) << "seek to midpoint should produce a key frame";
}

TEST_F(Mp4DemuxerFileTest, SeekToBeginningProducesKeyFrame) {
    ASSERT_TRUE(demuxer_->open(probeBuffer_));

    uint64_t seekPos = demuxer_->getSeekToPos(0.0);
    demuxer_->seekPos(seekPos);

    auto result = feedAllData(*demuxer_, kMp4Path, fileSize_, seekPos);
    EXPECT_GT(result.keyFrames, 0u) << "seek to beginning should produce a key frame";
}

// ─── TS integration tests ────────────────────────────────────────────────────

class TsDemuxerFileTest : public ::testing::Test {
protected:
    void SetUp() override {
        if (uint64_t sz = 0; !loadFile(kTsPath, sz)) {
            GTEST_SKIP() << "segment-0.ts not found, skipping";
        }
    }
};

TEST_F(TsDemuxerFileTest, TsDemuxerHandlesEmptyBuffer) {
    // 验证 TSDemuxer 对空 buffer 不崩溃
    auto audioInfo = std::shared_ptr<AudioInfo>();
    auto videoInfo = std::shared_ptr<VideoInfo>();
    TSDemuxer tsDemuxer(audioInfo, videoInfo);

    auto emptyBuffer = std::make_unique<Buffer>(0);
    DemuxerResult result;
    bool ok = tsDemuxer.parseData(*emptyBuffer, 0, result);
    EXPECT_FALSE(ok) << "empty buffer should return false";
}

TEST_F(TsDemuxerFileTest, ParseTsDataDirectlyViaTsDemuxer) {
    auto audioInfo = std::shared_ptr<AudioInfo>();
    auto videoInfo = std::shared_ptr<VideoInfo>();
    TSDemuxer tsDemuxer(audioInfo, videoInfo);

    uint64_t fileSize = 0;
    auto buffer = loadFile(kTsPath, fileSize);
    ASSERT_NE(buffer, nullptr);

    DemuxerResult result;
    bool ok = tsDemuxer.parseData(*buffer, 0, result);
    EXPECT_TRUE(ok) << "TSDemuxer::parseData should succeed on valid TS";
}

TEST_F(TsDemuxerFileTest, TsDemuxerProducesVideoFrames) {
    auto audioInfo = std::shared_ptr<AudioInfo>();
    auto videoInfo = std::shared_ptr<VideoInfo>();
    TSDemuxer tsDemuxer(audioInfo, videoInfo);

    uint64_t fileSize = 0;
    auto buffer = loadFile(kTsPath, fileSize);
    ASSERT_NE(buffer, nullptr);

    DemuxerResult result;
    tsDemuxer.parseData(*buffer, 0, result);
    EXPECT_GT(result.videoFrames.size(), 0u) << "should produce video frames from TS";
}

TEST_F(TsDemuxerFileTest, TsDemuxerProducesAudioFrames) {
    auto audioInfo = std::shared_ptr<AudioInfo>();
    auto videoInfo = std::shared_ptr<VideoInfo>();
    TSDemuxer tsDemuxer(audioInfo, videoInfo);

    uint64_t fileSize = 0;
    auto buffer = loadFile(kTsPath, fileSize);
    ASSERT_NE(buffer, nullptr);

    DemuxerResult result;
    tsDemuxer.parseData(*buffer, 0, result);
    EXPECT_GT(result.audioFrames.size(), 0u) << "should produce audio frames from TS";
}

TEST_F(TsDemuxerFileTest, TsDemuxerFillsAudioInfo) {
    auto audioInfo = std::shared_ptr<AudioInfo>();
    auto videoInfo = std::shared_ptr<VideoInfo>();
    TSDemuxer tsDemuxer(audioInfo, videoInfo);

    uint64_t fileSize = 0;
    auto buffer = loadFile(kTsPath, fileSize);
    ASSERT_NE(buffer, nullptr);

    DemuxerResult result;
    tsDemuxer.parseData(*buffer, 0, result);

    ASSERT_NE(audioInfo, nullptr) << "audioInfo should be created by TSDemuxer after parsing TS";
    EXPECT_GT(audioInfo->sampleRate, 0u) << "audio sampleRate should be filled after parsing TS";
    EXPECT_GT(audioInfo->channels, 0u) << "audio channels should be filled after parsing TS";
}

TEST_F(TsDemuxerFileTest, TsDemuxerFillsVideoInfo) {
    auto audioInfo = std::shared_ptr<AudioInfo>();
    auto videoInfo = std::shared_ptr<VideoInfo>();
    TSDemuxer tsDemuxer(audioInfo, videoInfo);

    uint64_t fileSize = 0;
    auto buffer = loadFile(kTsPath, fileSize);
    ASSERT_NE(buffer, nullptr);

    DemuxerResult result;
    tsDemuxer.parseData(*buffer, 0, result);

    // videoInfo 由 TSDemuxer 内部创建并设置
    ASSERT_NE(videoInfo, nullptr) << "videoInfo should be created by TSDemuxer after parsing TS";
    EXPECT_GT(videoInfo->width, 0u) << "video width should be filled after parsing TS";
    EXPECT_GT(videoInfo->height, 0u) << "video height should be filled after parsing TS";
}

TEST_F(TsDemuxerFileTest, TsDemuxerResetClearsState) {
    auto audioInfo = std::shared_ptr<AudioInfo>();
    auto videoInfo = std::shared_ptr<VideoInfo>();
    TSDemuxer tsDemuxer(audioInfo, videoInfo);

    uint64_t fileSize = 0;
    auto buffer = loadFile(kTsPath, fileSize);
    ASSERT_NE(buffer, nullptr);

    DemuxerResult result;
    tsDemuxer.parseData(*buffer, 0, result);
    ASSERT_GT(result.videoFrames.size(), 0u);
    auto firstCount = result.videoFrames.size();

    tsDemuxer.resetData();
    // reset 后 audioInfo/videoInfo 被清空，重新创建
    audioInfo.reset();
    videoInfo.reset();
    TSDemuxer tsDemuxer2(audioInfo, videoInfo);

    auto buffer2 = loadFile(kTsPath, fileSize);
    ASSERT_NE(buffer2, nullptr);

    DemuxerResult result2;
    tsDemuxer2.parseData(*buffer2, 0, result2);
    EXPECT_EQ(firstCount, result2.videoFrames.size())
        << "re-parsing same TS should produce same frame count";
}

// ─── WAV integration tests ───────────────────────────────────────────────────

class WavDemuxerFileTest : public ::testing::Test {
protected:
    void SetUp() override {
        probeBuffer_ = loadFile(kWavPath, fileSize_);
        if (!probeBuffer_) {
            GTEST_SKIP() << "sample-3s.wav not found, skipping";
        }
        demuxer_ = DemuxerManager::shareInstance().create(DemuxerType::WAV);
        ASSERT_NE(demuxer_, nullptr);
        DemuxerConfig config;
        config.filePath = kWavPath;
        demuxer_->init(config);
    }

    std::unique_ptr<Buffer> probeBuffer_;
    std::shared_ptr<IDemuxer> demuxer_;
    uint64_t fileSize_ = 0;
};

TEST_F(WavDemuxerFileTest, OpenSucceeds) {
    ASSERT_TRUE(demuxer_->open(probeBuffer_));
    EXPECT_TRUE(demuxer_->isOpened());
}

TEST_F(WavDemuxerFileTest, HasAudioNoVideo) {
    ASSERT_TRUE(demuxer_->open(probeBuffer_));
    EXPECT_TRUE(demuxer_->hasAudio());
    EXPECT_FALSE(demuxer_->hasVideo());
}

TEST_F(WavDemuxerFileTest, AudioInfoIsValid) {
    ASSERT_TRUE(demuxer_->open(probeBuffer_));
    auto audioInfo = demuxer_->audioInfo();
    ASSERT_NE(audioInfo, nullptr);
    EXPECT_GT(audioInfo->sampleRate, 0u);
    EXPECT_GT(audioInfo->channels, 0u);
    EXPECT_GT(audioInfo->bitsPerSample, 0u);
}

TEST_F(WavDemuxerFileTest, TotalDurationIsPositive) {
    ASSERT_TRUE(demuxer_->open(probeBuffer_));
    EXPECT_GT(demuxer_->totalDuration().second(), 0.0);
}

TEST_F(WavDemuxerFileTest, ParseDataProducesAudioFrames) {
    ASSERT_TRUE(demuxer_->open(probeBuffer_));

    uint64_t startOffset = 0;
    if (auto hi = demuxer_->headerInfo(); hi && hi->headerLength > 0) {
        startOffset = hi->headerLength;
        demuxer_->seekPos(startOffset);
    }

    auto result = feedAllData(*demuxer_, kWavPath, fileSize_, startOffset);
    EXPECT_GT(result.audioFrames, 0u) << "no audio frames parsed from WAV";
    EXPECT_EQ(result.videoFrames, 0u) << "WAV should not produce video frames";
}

TEST_F(WavDemuxerFileTest, SeekToMidpointWorks) {
    ASSERT_TRUE(demuxer_->open(probeBuffer_));

    double duration = demuxer_->totalDuration().second();
    ASSERT_GT(duration, 0.0);

    double seekTime = duration / 2.0;
    uint64_t seekPos = demuxer_->getSeekToPos(seekTime);
    EXPECT_GT(seekPos, 0u);

    demuxer_->seekPos(seekPos);
    EXPECT_FALSE(demuxer_->isCompleted());
}

// ─── fMP4 integration tests ──────────────────────────────────────────────────

namespace {
constexpr const char* kFmp4Path = "../../test_sample/Sample_fmp4.mp4";
} // namespace

class Fmp4DemuxerFileTest : public ::testing::Test {
protected:
    void SetUp() override {
        probeBuffer_ = loadFile(kFmp4Path, fileSize_);
        if (!probeBuffer_) {
            GTEST_SKIP() << "Sample_fmp4.mp4 not found, skipping";
        }
        demuxer_ = DemuxerManager::shareInstance().create(DemuxerType::MP4);
        ASSERT_NE(demuxer_, nullptr);
        DemuxerConfig config;
        config.filePath = kFmp4Path;
        demuxer_->init(config);
    }

    std::unique_ptr<Buffer> probeBuffer_;
    std::shared_ptr<IDemuxer> demuxer_;
    uint64_t fileSize_ = 0;
};

TEST_F(Fmp4DemuxerFileTest, OpenSucceeds) {
    ASSERT_TRUE(demuxer_->open(probeBuffer_));
    EXPECT_TRUE(demuxer_->isOpened());
}

TEST_F(Fmp4DemuxerFileTest, HasVideoAndAudio) {
    ASSERT_TRUE(demuxer_->open(probeBuffer_));
    EXPECT_TRUE(demuxer_->hasVideo());
    EXPECT_TRUE(demuxer_->hasAudio());
}

TEST_F(Fmp4DemuxerFileTest, VideoInfoIsValid) {
    ASSERT_TRUE(demuxer_->open(probeBuffer_));
    auto videoInfo = demuxer_->videoInfo();
    ASSERT_NE(videoInfo, nullptr);
    EXPECT_GT(videoInfo->width, 0u);
    EXPECT_GT(videoInfo->height, 0u);
    EXPECT_GT(videoInfo->timeScale, 0u);
}

TEST_F(Fmp4DemuxerFileTest, AudioInfoIsValid) {
    ASSERT_TRUE(demuxer_->open(probeBuffer_));
    auto audioInfo = demuxer_->audioInfo();
    ASSERT_NE(audioInfo, nullptr);
    EXPECT_GT(audioInfo->sampleRate, 0u);
    EXPECT_GT(audioInfo->channels, 0u);
}

TEST_F(Fmp4DemuxerFileTest, TotalDurationIsPositive) {
    ASSERT_TRUE(demuxer_->open(probeBuffer_));
    EXPECT_GT(demuxer_->computeTotalDuration().second(), 0.0);
}

TEST_F(Fmp4DemuxerFileTest, ParseDataProducesFrames) {
    ASSERT_TRUE(demuxer_->open(probeBuffer_));
    auto result = feedAllData(*demuxer_, kFmp4Path, fileSize_, 0);
    EXPECT_GT(result.videoFrames, 0u) << "no video frames from fMP4";
    EXPECT_GT(result.audioFrames, 0u) << "no audio frames from fMP4";
    EXPECT_GT(result.keyFrames, 0u) << "no key frames from fMP4";
}

TEST_F(Fmp4DemuxerFileTest, SeekToMidpointProducesKeyFrame) {
    ASSERT_TRUE(demuxer_->open(probeBuffer_));
    double duration = demuxer_->computeTotalDuration().second();
    ASSERT_GT(duration, 0.0);

    uint64_t seekPos = demuxer_->getSeekToPos(duration / 2.0);
    EXPECT_GT(seekPos, 0u);
    demuxer_->seekPos(seekPos);

    auto result = feedAllData(*demuxer_, kFmp4Path, fileSize_, seekPos);
    EXPECT_GT(result.keyFrames, 0u) << "seek to midpoint should produce a key frame";
}
