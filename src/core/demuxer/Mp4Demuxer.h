//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#pragma once

#include <optional>

#include "IDemuxer.h"
#include "MediaDefs.h"
#include "Mp4Box.h"

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

    std::shared_ptr<BoxElst> elst;
    int64_t elstMediaTimeOffset = 0;

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

    bool isFragmented = false;
    std::shared_ptr<BoxTrex> trex;
    uint32_t trackId = 0;

    struct FragSample {
        uint64_t offset{};
        uint32_t size{};
        uint32_t duration{};
        uint32_t flags{};
        int32_t compositionTimeOffset{};
        int64_t dts{};
        int64_t pts{};
        bool isKeyFrame{};
    };

    std::vector<FragSample> fragSamples;
    uint32_t fragSampleIndex = 0;

    struct SidxSegment {
        uint64_t offset{};
        uint64_t duration{};
        uint64_t startTime{};
    };

    std::vector<SidxSegment> sidxSegments;
    // fMP4: 从所有 fragment sample 累加的总 duration（timeScale 单位 ticks）
    uint64_t fragmentDurationTicks = 0;
    int32_t rotationDegrees = 0;

 public:
    bool isInRange(Buffer& buffer, uint64_t* outOffset = nullptr) const noexcept;

    void reset() noexcept;

    void seek(uint64_t pos) noexcept;

    void seekToFragment(uint64_t pos) noexcept;

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

    void appendFragmentSamples(const BoxRefPtr& moofBox, uint64_t moofOffset) noexcept;

 private:
    using NaluClassifier = std::function<std::pair<bool, VideoFrameType>(uint8_t naluType)>;

    AVFramePtrArray parseNaluFrames(AVFramePtr frame,
                                    DataPtr data,
                                    std::shared_ptr<VideoFrameInfo> frameInfo,
                                    const NaluClassifier& classifier);

    void parseVideoFramePayload(AVFramePtr& frame,
                                DataPtr data,
                                std::shared_ptr<VideoFrameInfo> videoInfo,
                                AVFramePtrArray& packets);

    uint32_t findSampleIndexByTime(double targetTime) const noexcept;

    std::optional<std::pair<uint32_t, uint32_t>> findChunkBySampleIndex(
        uint32_t seekSampleIndex) const noexcept;

    void seekToSample(uint32_t targetSampleIndex) noexcept;

    void parseFragmentedData(Buffer& buffer,
                             std::shared_ptr<FrameInfo> frameInfo,
                             AVFramePtrArray& packets);

    void parseNormalData(Buffer& buffer,
                         uint64_t start,
                         std::shared_ptr<FrameInfo> frameInfo,
                         AVFramePtrArray& packets);

    void appendTrafSamples(const BoxRefPtr& trafChild, uint64_t moofOffset) noexcept;

    // 返回处理后的下一个 trun 的 baseDecodeTime
    int64_t appendTrunSamples(const std::shared_ptr<BoxTrun>& trunBox,
                              const std::shared_ptr<BoxTfhd>& tfhdBox,
                              uint64_t baseOffset,
                              int64_t baseDecodeTime) noexcept;

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

    void flush() noexcept override;

    void seekPos(uint64_t pos) noexcept override;

    DemuxerResult parseData(DataPacket& packet) noexcept override;

    [[nodiscard]] uint64_t getSeekToPos(double time) noexcept override;

    [[nodiscard]] CTime computeTotalDuration() const noexcept override;

    inline static const DemuxerInfo& info() noexcept {
        static DemuxerInfo info = {DemuxerType::MP4, "ftyp",
                                   Reflection::enroll<Mp4Demuxer>("Mp4Demuxer"), 4};
        return info;
    }

    bool probeMoovBox(Buffer& buffer, int64_t& start, uint32_t& size) noexcept;

    /// debug info
    [[nodiscard]] std::string description() const noexcept;

 private:
    bool parseMoovBox(Buffer& buffer, const BoxRefPtr& moovBox) noexcept;

    bool parseMoofBox(Buffer& buffer, const BoxRefPtr& moofBox) noexcept;

    void parseMoofChildren(Buffer& buffer, const BoxRefPtr& moofBox) noexcept;

    void initData() noexcept;

    void initAudioTrack(const std::shared_ptr<TrackContext>& track) noexcept;

    void initVideoTrack(const std::shared_ptr<TrackContext>& track) noexcept;
    void initVideoFps(const std::shared_ptr<TrackContext>& track) noexcept;

    bool isCompleted() const noexcept;

    std::shared_ptr<AudioFrameInfo> makeAudioFrameInfo() const noexcept;

    std::shared_ptr<VideoFrameInfo> makeVideoFrameInfo() const noexcept;

    void handleBoxInMoov(const BoxRefPtr& box,
                         std::shared_ptr<BoxMdhd>& mdhdBox,
                         std::stack<BoxRefPtr>& parentBox) noexcept;

    void parseTkhd(const BoxRefPtr& box, Buffer& buffer, uint32_t& trackId) noexcept;

    void attachStsdToTracks(
        uint32_t trackId,
        const std::unordered_map<uint32_t, std::shared_ptr<BoxElst>>& pendingElst) noexcept;

    bool advancePastBox(const BoxRefPtr& box,
                        std::stack<BoxRefPtr>& parentBox,
                        Buffer& buffer,
                        bool& res) noexcept;

    bool recoverFromSkipFailure(std::stack<BoxRefPtr>& parentBox,
                                Buffer& buffer,
                                bool& res) noexcept;

    // 返回 false 时调用方应终止 open 解析
    bool handleMdatBox(const BoxRefPtr& box, Buffer& buffer, bool res, bool& eof) noexcept;

    void handleSidxBox(const BoxRefPtr& box, Buffer& buffer) noexcept;

    // open() 的 box 分发 + skip 逻辑，返回 false 时终止循环
    bool processOpenBox(const BoxRefPtr& box, Buffer& buffer, bool& res, bool& eof) noexcept;

    [[nodiscard]] std::shared_ptr<TrackContext> findEarliestTrack() const noexcept;

    bool advanceStaleTracks() noexcept;

    [[nodiscard]] uint64_t seekPosFragmented(double time, uint64_t fallbackOffset) const noexcept;

    [[nodiscard]] uint64_t seekPosNormal(double time, uint64_t fallbackOffset) const noexcept;

 private:
    BoxRefPtr rootBox_;
    std::unordered_map<CodecId, std::shared_ptr<TrackContext>> tracks_;
    bool isFragmented_ = false;
    std::unordered_map<uint32_t, int32_t> trackRotations_;
};

}  // namespace slark
