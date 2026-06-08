//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#pragma once

#include "Buffer.hpp"

namespace slark {

enum class CodecId : uint32_t {
    Unknown = 0,
    AVC = 0x1b,
    VP9 = 0xa7,
    HEVC = 0xad,
    MP3 = 0x15001,
    AAC = 0x15002,
    OPUS = 0x1503c,
};

class Box;
using BoxRefPtr = std::shared_ptr<Box>;
using BoxPtr = std::unique_ptr<Box>;

struct BoxInfo {
    uint64_t start = 0;
    uint32_t type = 0;
    uint64_t size = 0;
    uint32_t headerSize = 8;
    std::string symbol;

    [[nodiscard]] uint64_t end() const noexcept {
        return start + size;
    }
};

class Box {
public:
    std::vector<BoxRefPtr> childs;
    BoxInfo info;

public:
    Box(BoxInfo&& boxInfo) : info(boxInfo) {}

    virtual ~Box() = default;

public:
    virtual bool decode(Buffer&) noexcept;
    [[nodiscard]] virtual std::string description(const std::string&) const noexcept;

    void append(std::shared_ptr<Box> child) noexcept;
    [[nodiscard]] virtual BoxRefPtr getChild(std::string_view type) const noexcept;

public:
    static BoxRefPtr createBox(Buffer&);
    static std::expected<BoxInfo, bool> tryParseBoxInfo(Buffer&) noexcept;
};

class BoxFtyp : public Box {
public:
    std::string majorBrand{};
    uint32_t minorVersion{};
    std::string compatibleBrands{};

public:
    BoxFtyp(BoxInfo&& boxInfo) : Box(std::move(boxInfo)) {}

    ~BoxFtyp() override = default;

public:
    bool decode(Buffer&) noexcept override;
    std::string description(const std::string&) const noexcept override;
};

class BoxMvhd : public Box {
public:
    uint8_t version{};
    uint32_t flags{};
    uint64_t creationTime{};
    uint64_t modificationTime{};
    uint32_t timeScale{};
    uint64_t duration{};
    float preferredRate{};
    float preferredVolume{};
    std::string reserved{};
    std::vector<float> matrix;
    uint32_t previewTime{};
    uint32_t previewDuration{};
    uint32_t posterTime{};
    uint32_t selectionTime{};
    uint32_t selectionDuration{};
    uint32_t currentTime{};
    uint32_t nextTrackId{};

public:
    BoxMvhd(BoxInfo&& boxInfo) : Box(std::move(boxInfo)), matrix(9, 0) {}

    ~BoxMvhd() override = default;

public:
    bool decode(Buffer&) noexcept override;
    std::string description(const std::string&) const noexcept override;

private:
    std::string matrixString(const std::string&) const noexcept;
};

class BoxMdhd : public Box {
public:
    uint8_t version{};
    uint32_t flags{};
    uint64_t creationTime{};
    uint64_t modificationTime{};
    uint32_t timeScale{};
    uint64_t duration{};
    uint16_t language{};
    uint16_t quality{};

public:
    BoxMdhd(BoxInfo&& boxInfo) : Box(std::move(boxInfo)) {}

    ~BoxMdhd() override = default;

public:
    bool decode(Buffer&) noexcept override;
    std::string description(const std::string&) const noexcept override;
};

// Sample Description
class BoxStsd : public Box {
public:
    bool isAudio = false;
    bool isVideo = false;
    uint8_t version{};
    uint32_t flags{};
    uint32_t entryCount{};
    uint16_t dataReferenceIndex{};

    uint16_t frameCount{};
    // video info
    uint16_t width{};
    uint16_t height{};

    // audio info
    uint16_t channelCount{};
    uint16_t sampleSize{};
    uint16_t audioCompressionId{};
    uint16_t packetSize{};
    uint16_t sampleRate{};

public:
    BoxStsd(BoxInfo&& boxInfo) : Box(std::move(boxInfo)) {}

    ~BoxStsd() override = default;

    std::tuple<CodecId, std::string> getCodecId();

public:
    bool decode(Buffer&) noexcept override;

private:
    void decodeAudioEntry(Buffer& buffer, std::string_view symbol) noexcept;
    void decodeVideoEntry(Buffer& buffer, std::string_view symbol) noexcept;
};

struct StscEntry {
    uint32_t firstChunk{};
    uint32_t samplesPerChunk{};
    uint32_t sampleDescriptionIndex{};
};

// Sample - to - Chunk
class BoxStsc : public Box {
public:
    std::vector<StscEntry> entrys;

public:
    BoxStsc(BoxInfo&& boxInfo) : Box(std::move(boxInfo)) {}

    ~BoxStsc() override = default;

public:
    bool decode(Buffer&) noexcept override;
};

// Sample Size
class BoxStsz : public Box {
public:
    std::vector<uint64_t> sampleSizes;

public:
    BoxStsz(BoxInfo&& boxInfo) : Box(std::move(boxInfo)) {}

    ~BoxStsz() override = default;

public:
    bool decode(Buffer&) noexcept override;
};

// Chunk Offset
class BoxStco : public Box {
public:
    std::vector<uint64_t> chunkOffsets;

public:
    BoxStco(BoxInfo&& boxInfo) : Box(std::move(boxInfo)) {}

    ~BoxStco() override = default;

public:
    bool decode(Buffer&) noexcept override;
};

// 64-bit Chunk Offset (for files > 4GB)
class BoxCo64 : public Box {
public:
    std::vector<uint64_t> chunkOffsets;

public:
    BoxCo64(BoxInfo&& boxInfo) : Box(std::move(boxInfo)) {}

    ~BoxCo64() override = default;

public:
    bool decode(Buffer&) noexcept override;
};

struct SttsEntry {
    uint32_t sampleCount{};
    uint32_t sampleDelta{};
};

// Time - to - Sample
class BoxStts : public Box {
public:
    std::vector<SttsEntry> entrys;

public:
    BoxStts(BoxInfo&& boxInfo) : Box(std::move(boxInfo)) {}

    ~BoxStts() override = default;

public:
    bool decode(Buffer&) noexcept override;
    uint32_t sampleSize() const noexcept;
};

struct CttsEntry {
    uint32_t sampleCount{};
    int32_t sampleOffset{};
};

class BoxCtts : public Box {
public:
    uint8_t version{};
    std::vector<CttsEntry> entrys;

public:
    BoxCtts(BoxInfo&& boxInfo) : Box(std::move(boxInfo)) {}

    ~BoxCtts() override = default;

public:
    bool decode(Buffer&) noexcept override;
};

class BoxStss : public Box {
public:
    std::vector<uint32_t> keyIndexs;

public:
    BoxStss(BoxInfo&& boxInfo) : Box(std::move(boxInfo)) {}

    ~BoxStss() override = default;

public:
    bool decode(Buffer&) noexcept override;
};

struct AudioSpecificConfig {
    uint8_t channelConfiguration{};
    uint32_t samplingFrequencyIndex{};
    uint8_t audioObjectType{};
    uint32_t samplingFreqIndexExt{};
    uint8_t audioObjectTypeExt{};
};

class BoxEsds : public Box {
public:
    uint8_t objectTypeId{};
    CodecId codecId{};
    AudioSpecificConfig audioSpecConfig;
    uint32_t bufferSize{};
    uint32_t maxBitRate{};
    uint32_t avgBitRate{};

public:
    BoxEsds(BoxInfo&& boxInfo) : Box(std::move(boxInfo)) {}

    ~BoxEsds() override = default;

public:
    bool decode(Buffer&) noexcept override;
    std::string description(const std::string&) const noexcept override;

private:
    void decodeAudioSpecificConfig(Buffer&) noexcept;
    int32_t getDescrLen(Buffer&) noexcept;
};

class BoxMeta : public Box {
public:
    uint8_t version{};
    uint32_t flags{};

public:
    BoxMeta(BoxInfo&& boxInfo) : Box(std::move(boxInfo)) {}

    ~BoxMeta() override = default;

public:
    bool decode(Buffer&) noexcept override;
};

class BoxAvcc : public Box {
public:
    uint8_t version{};
    uint8_t profileIdc{};
    uint8_t profileCompatibility{};
    uint8_t levelIdc{};
    uint8_t naluByteSize{};

    std::vector<DataRefPtr> sps;
    std::vector<DataRefPtr> pps;

public:
    BoxAvcc(BoxInfo&& boxInfo) : Box(std::move(boxInfo)) {}

    ~BoxAvcc() override = default;

public:
    bool decode(Buffer&) noexcept override;
    std::string description(const std::string&) const noexcept override;
};

class BoxHvcc : public Box {
public:
    uint8_t version{};
    uint8_t profileSpace{};
    uint8_t tierFlag{};
    uint8_t profileIdc{};
    uint32_t profileCompatibility{};
    uint8_t levelIdc{};
    uint16_t avgFrameRate{};
    uint8_t naluByteSize{};

    std::vector<DataRefPtr> sps;
    std::vector<DataRefPtr> pps;
    std::vector<DataRefPtr> vps;

public:
    BoxHvcc(BoxInfo&& boxInfo) : Box(std::move(boxInfo)) {}

    ~BoxHvcc() override = default;

public:
    bool decode(Buffer&) noexcept override;
    std::string description(const std::string&) const noexcept override;
};

struct ElstEntry {
    uint64_t segmentDuration{};
    int64_t mediaTime{};
    int16_t mediaRateInteger{1};
    int16_t mediaRateFraction{0};
};

class BoxElst : public Box {
public:
    uint8_t version{};
    std::vector<ElstEntry> entries;

public:
    BoxElst(BoxInfo&& boxInfo) : Box(std::move(boxInfo)) {}

    ~BoxElst() override = default;

public:
    bool decode(Buffer&) noexcept override;
    [[nodiscard]] int64_t mediaTimeOffset() const noexcept;
    [[nodiscard]] bool hasEmptyEdit() const noexcept;
};

class BoxTkhd : public Box {
public:
    uint8_t version{};
    uint32_t flags{};
    uint64_t creationTime{};
    uint64_t modificationTime{};
    uint32_t trackId{};
    uint64_t duration{};
    int16_t layer{};
    int16_t alternateGroup{};
    int16_t volume{};
    std::vector<float> matrix{9, 0};
    uint32_t width{};
    uint32_t height{};

public:
    BoxTkhd(BoxInfo&& boxInfo) : Box(std::move(boxInfo)) {}
    ~BoxTkhd() override = default;

    bool decode(Buffer&) noexcept override;
};

class BoxHdlr : public Box {
public:
    uint8_t version{};
    uint32_t flags{};
    std::string handlerType; // "vide", "soun", "hint", etc.

public:
    BoxHdlr(BoxInfo&& boxInfo) : Box(std::move(boxInfo)) {}
    ~BoxHdlr() override = default;

    bool decode(Buffer&) noexcept override;
};

class BoxVmhd : public Box {
public:
    uint8_t version{};
    uint32_t flags{};

public:
    BoxVmhd(BoxInfo&& boxInfo) : Box(std::move(boxInfo)) {}
    ~BoxVmhd() override = default;

    bool decode(Buffer&) noexcept override;
};

class BoxSmhd : public Box {
public:
    uint8_t version{};
    uint32_t flags{};

public:
    BoxSmhd(BoxInfo&& boxInfo) : Box(std::move(boxInfo)) {}
    ~BoxSmhd() override = default;

    bool decode(Buffer&) noexcept override;
};

class BoxDref : public Box {
public:
    uint8_t version{};
    uint32_t flags{};

public:
    BoxDref(BoxInfo&& boxInfo) : Box(std::move(boxInfo)) {}
    ~BoxDref() override = default;

    bool decode(Buffer&) noexcept override;
};

class BoxMvex : public Box {
public:
    BoxMvex(BoxInfo&& boxInfo) : Box(std::move(boxInfo)) {}

    ~BoxMvex() override = default;
};

class BoxTrex : public Box {
public:
    uint8_t version{};
    uint32_t trackId{};
    uint32_t defaultSampleDescriptionIndex{1};
    uint32_t defaultSampleDuration{};
    uint32_t defaultSampleSize{};
    uint32_t defaultSampleFlags{};

public:
    BoxTrex(BoxInfo&& boxInfo) : Box(std::move(boxInfo)) {}

    ~BoxTrex() override = default;

public:
    bool decode(Buffer&) noexcept override;
};

class BoxMoof : public Box {
public:
    BoxMoof(BoxInfo&& boxInfo) : Box(std::move(boxInfo)) {}

    ~BoxMoof() override = default;
};

class BoxMfhd : public Box {
public:
    uint8_t version{};
    uint32_t sequenceNumber{};

public:
    BoxMfhd(BoxInfo&& boxInfo) : Box(std::move(boxInfo)) {}

    ~BoxMfhd() override = default;

public:
    bool decode(Buffer&) noexcept override;
};

class BoxTraf : public Box {
public:
    BoxTraf(BoxInfo&& boxInfo) : Box(std::move(boxInfo)) {}

    ~BoxTraf() override = default;
};

class BoxTfhd : public Box {
public:
    static constexpr uint32_t kBaseDataOffsetPresent = 0x000001;
    static constexpr uint32_t kSampleDescriptionIndexPresent = 0x000002;
    static constexpr uint32_t kDefaultSampleDurationPresent = 0x000008;
    static constexpr uint32_t kDefaultSampleSizePresent = 0x000010;
    static constexpr uint32_t kDefaultSampleFlagsPresent = 0x000020;
    static constexpr uint32_t kDurationIsEmpty = 0x010000;
    static constexpr uint32_t kDefaultBaseIsMoof = 0x020000;

    uint8_t version{};
    uint32_t flags{};
    uint32_t trackId{};
    uint64_t baseDataOffset{};
    uint32_t sampleDescriptionIndex{1};
    uint32_t defaultSampleDuration{};
    uint32_t defaultSampleSize{};
    uint32_t defaultSampleFlags{};

public:
    BoxTfhd(BoxInfo&& boxInfo) : Box(std::move(boxInfo)) {}

    ~BoxTfhd() override = default;

public:
    bool decode(Buffer&) noexcept override;
};

class BoxTfdt : public Box {
public:
    uint8_t version{};
    uint64_t baseMediaDecodeTime{};

public:
    BoxTfdt(BoxInfo&& boxInfo) : Box(std::move(boxInfo)) {}

    ~BoxTfdt() override = default;

public:
    bool decode(Buffer&) noexcept override;
};

struct TrunSample {
    uint32_t duration{};
    uint32_t size{};
    uint32_t flags{};
    int32_t compositionTimeOffset{};
};

class BoxTrun : public Box {
public:
    static constexpr uint32_t kDataOffsetPresent = 0x000001;
    static constexpr uint32_t kFirstSampleFlagsPresent = 0x000004;
    static constexpr uint32_t kSampleDurationPresent = 0x000100;
    static constexpr uint32_t kSampleSizePresent = 0x000200;
    static constexpr uint32_t kSampleFlagsPresent = 0x000400;
    static constexpr uint32_t kSampleCompositionTimeOffsetPresent = 0x000800;
    static constexpr uint32_t kSampleIsNonSync = 0x00010000;

    uint8_t version{};
    uint32_t flags{};
    int32_t dataOffset{};
    uint32_t firstSampleFlags{};
    std::vector<TrunSample> samples;

public:
    BoxTrun(BoxInfo&& boxInfo) : Box(std::move(boxInfo)) {}

    ~BoxTrun() override = default;

public:
    bool decode(Buffer&) noexcept override;
};

struct SidxEntry {
    bool referenceType{};
    uint32_t referencedSize{};
    uint32_t subsegmentDuration{};
    bool startsWithSap{};
    uint8_t sapType{};
    uint32_t sapDeltaTime{};
};

class BoxSidx : public Box {
public:
    uint8_t version{};
    uint32_t referenceId{};
    uint32_t timeScale{};
    uint64_t earliestPresentationTime{};
    uint64_t firstOffset{};
    std::vector<SidxEntry> entries;

public:
    BoxSidx(BoxInfo&& boxInfo) : Box(std::move(boxInfo)) {}

    ~BoxSidx() override = default;

public:
    bool decode(Buffer&) noexcept override;
};

} // namespace slark
