//
//  Mp4Demuxer.h
//  slark
//
//  Created by Nevermore
//

#pragma once

#include "IDemuxer.h"
#include "MediaDefs.h"
#include "Mp4Box.h"

#include <optional>

namespace slark {

class TrackContext {
public:
    bool isCompleted = false;
    TrackType type = TrackType::Unknown;
    CodecId codecId = CodecId::Unknown;
    std::string mediaBoxName;
    std::shared_ptr<BoxMdhd> mdhd;
    std::shared_ptr<Box> stbl;
    std::shared_ptr<BoxStco> stco;
    std::shared_ptr<BoxStsz> stsz;
    std::shared_ptr<BoxStsc> stsc;
    std::shared_ptr<BoxStsd> stsd;
    std::shared_ptr<BoxStts> stts;
    std::shared_ptr<BoxCtts> ctts;
    std::shared_ptr<BoxStss> stss;

    uint32_t stszSampleSizeIndex = 0;
    uint32_t stscEntryIndex = 0;
    uint32_t entrySampleIndex = 0;
    uint32_t chunkLogicIndex = 0;
    uint32_t sampleOffset = 0;

    uint32_t sttsEntryIndex = 0;
    uint32_t sttsEntrySampleIndex = 0;

    uint32_t cttsEntryIndex = 0;
    uint32_t cttsEntrySampleIndex = 0;

    int64_t dts = 0;
    int64_t pts = 0;
    uint64_t index = 0;
    uint64_t keyIndex = 0;
    uint16_t naluByteSize = 0;

public:
    /// @return true if current sample is fully within buffer; optionally outputs sample start
    /// offset
    bool isInRange(Buffer& buffer, uint64_t* outOffset = nullptr) const noexcept;

    void reset() noexcept;

    void seek(uint64_t pos) noexcept;

    uint64_t getSeekPos(double time) const noexcept;

    void parseData(Buffer& buffer, std::shared_ptr<FrameInfo> frameInfo, AVFramePtrArray& packets);

    AVFramePtrArray parseH264FrameData(AVFramePtr frame,
                                       DataPtr data,
                                       std::shared_ptr<VideoFrameInfo> frameInfo);

    AVFramePtrArray parseH265FrameData(AVFramePtr frame,
                                       DataPtr data,
                                       std::shared_ptr<VideoFrameInfo> frameInfo);

    void calcIndex() noexcept;

    void init() noexcept;

private:
    using NaluClassifier = std::function<std::pair<bool, VideoFrameType>(uint8_t naluType)>;

    AVFramePtrArray parseNaluFrames(AVFramePtr frame,
                                    DataPtr data,
                                    std::shared_ptr<VideoFrameInfo> frameInfo,
                                    const NaluClassifier& classifier);

    uint32_t findSampleIndexByTime(double targetTime) const noexcept;

    std::optional<std::pair<uint32_t, uint32_t>> findChunkBySampleIndex(
        uint32_t seekSampleIndex) const noexcept;

    void seekToSample(uint32_t targetSampleIndex) noexcept;

    /// calcIndex 的三个独立状态机推进
    void advanceStscState() noexcept;
    void advanceSttsState() noexcept;
    void advanceCttsState() noexcept;
};

class Mp4Demuxer : public IDemuxer {
public:
    Mp4Demuxer() {
        type_ = DemuxerType::MP4;
    }

    ~Mp4Demuxer() override;

    bool open(std::unique_ptr<Buffer>& buffer) noexcept override;

    void close() noexcept override;

    void seekPos(uint64_t pos) noexcept override;

    DemuxerResult parseData(DataPacket& packet) noexcept override;

    [[nodiscard]] uint64_t getSeekToPos(double time) noexcept override;

    inline static const DemuxerInfo& info() noexcept {
        static DemuxerInfo info = {DemuxerType::MP4,
                                   "ftyp",
                                   Reflection::enroll<Mp4Demuxer>("Mp4Demuxer"),
                                   4};
        return info;
    }

    bool probeMoovBox(Buffer& buffer, int64_t& start, uint32_t& size) noexcept;

    /// debug info
    [[nodiscard]] std::string description() const noexcept;

private:
    bool parseMoovBox(Buffer& buffer, const BoxRefPtr& moovBox) noexcept;

    void initData() noexcept;

    void initAudioTrack(const std::shared_ptr<TrackContext>& track) noexcept;

    void initVideoTrack(const std::shared_ptr<TrackContext>& track) noexcept;

    bool isCompleted() const noexcept;

    std::shared_ptr<AudioFrameInfo> makeAudioFrameInfo() const noexcept;

    std::shared_ptr<VideoFrameInfo> makeVideoFrameInfo() const noexcept;

    /// 解析 moov 时处理单个 box 的状态更新
    void handleBoxInMoov(const BoxRefPtr& box,
                         std::shared_ptr<BoxMdhd>& mdhdBox,
                         std::stack<BoxRefPtr>& parentBox) noexcept;

private:
    BoxRefPtr rootBox_;
    std::unordered_map<CodecId, std::shared_ptr<TrackContext>> tracks_;
};

} // namespace slark
