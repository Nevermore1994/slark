//
//  Mp4Box.hpp
//  slark
//
//  Created by Nevermore
//

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
    uint32_t start = 0;
    uint32_t type = 0;
    uint64_t size = 0;
    uint32_t headerSize = 8;
    std::string symbol;
};

class Box
{
public:
    std::vector<BoxRefPtr> childs;
    BoxInfo info;
public:
    Box(BoxInfo&& boxInfo)
        : info(boxInfo) {
        
    }
    virtual ~Box() = default;
    
public:
    virtual bool decode(Buffer&) noexcept;
    [[nodiscard]] virtual std::string description(const std::string&) const noexcept; //debug
    
    void append(std::shared_ptr<Box> child) noexcept;
    [[nodiscard]] virtual BoxRefPtr getChild(std::string_view type) const noexcept;
    
public:
    static BoxRefPtr createBox(Buffer&);
    static std::expected<BoxInfo, bool> tryParseBoxInfo(Buffer&) noexcept;
};

class BoxFtyp : public Box
{
public:
    std::string majorBrand{};
    uint32_t minorVersion{};
    std::string compatibleBrands{};
public:
    BoxFtyp(BoxInfo&& boxInfo)
        : Box(std::move(boxInfo)) {
        
    }
    ~BoxFtyp() override  = default;

public:
    bool decode(Buffer&) noexcept override;
    std::string description(const std::string& ) const noexcept override;
};

class BoxMvhd : public Box
{
public:
    uint8_t version{};
    uint32_t flags{};
    uint32_t creationTime{};
    uint32_t modificationTime{};
    uint32_t timeScale{};
    uint32_t duration{};
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
    BoxMvhd(BoxInfo&& boxInfo)
        : Box(std::move(boxInfo))
        , matrix(9, 0) {
        
    }
    ~BoxMvhd() override = default;

public:
    bool decode(Buffer&) noexcept override;
    std::string description(const std::string&) const noexcept override;

private:
    std::string matrixString(const std::string&) const noexcept;
};

class BoxMdhd : public Box
{
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
    BoxMdhd(BoxInfo&& boxInfo)
        : Box(std::move(boxInfo)) {
        
    }
    ~BoxMdhd() override = default;

public:
    bool decode(Buffer&) noexcept override;
    std::string description(const std::string&) const noexcept override;
};

///Sample Description
class BoxStsd : public Box
{
public:
    bool isAudio = false;
    bool isVideo = false;
    uint8_t version{};
    uint32_t flags{};
    uint32_t entryCount{};
    uint16_t dataReferenceIndex{};
    
    uint16_t frameCount{};
    //video info
    uint16_t width{};
    uint16_t height{};
    
    //audio info
    uint16_t channelCount{};
    uint16_t sampleSize{};
    uint16_t audioCompressionId{};
    uint16_t packetSize{};
    uint16_t sampleRate{};
public:
    BoxStsd(BoxInfo&& boxInfo)
        : Box(std::move(boxInfo)) {
        
    }
    ~BoxStsd() override = default;

    virtual CodecId getCodecId();

public:
    bool decode(Buffer&) noexcept override;
};

struct StscEntry
{
    uint32_t firstChunk{};
    uint32_t samplesPerChunk{};
    uint32_t sampleDescriptionIndex{};
};

///Sample-to-Chunk
class BoxStsc : public Box
{
public:
    std::vector<StscEntry> entrys;
public:
    BoxStsc(BoxInfo&& boxInfo)
        : Box(std::move(boxInfo)) {
        
    }
    ~BoxStsc() override = default;

public:
    bool decode(Buffer&) noexcept override;
};

///Sample Size
class BoxStsz : public Box
{
public:
    std::vector<uint64_t> sampleSizes;
public:
    BoxStsz(BoxInfo&& boxInfo)
        : Box(std::move(boxInfo)) {
        
    }
    ~BoxStsz() override = default;

public:
    bool decode(Buffer&) noexcept override;
};

///Chunk Offset
class BoxStco : public Box
{
public:
    std::vector<uint64_t> chunkOffsets;
public:
    BoxStco(BoxInfo&& boxInfo)
        : Box(std::move(boxInfo)) {
        
    }
    ~BoxStco() override = default;

public:
    bool decode(Buffer&) noexcept override;
};

struct SttsEntry
{
    uint32_t sampleCount{};
    uint32_t sampleDelta{};
};

///Time-to-Sample
class BoxStts : public Box
{
public:
    std::vector<SttsEntry> entrys;
public:
    BoxStts(BoxInfo&& boxInfo)
        : Box(std::move(boxInfo)) {
        
    }
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
    BoxCtts(BoxInfo&& boxInfo)
        : Box(std::move(boxInfo)) {
        
    }
    ~BoxCtts() override = default;

public:
    bool decode(Buffer&) noexcept override;
};

class BoxStss : public Box {
public:
    std::vector<uint32_t> keyIndexs;
public:
    BoxStss(BoxInfo&& boxInfo)
        : Box(std::move(boxInfo)) {
        
    }
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

class BoxEsds : public Box
{
public:
    uint8_t objectTypeId{};
    CodecId codecId{};
    AudioSpecificConfig audioSpecConfig;
    uint32_t bufferSize {};
    uint32_t maxBitRate {};
    uint32_t avgBitRate {};
public:
    BoxEsds(BoxInfo&& boxInfo)
        : Box(std::move(boxInfo)) {
        
    }
    ~BoxEsds() override = default;

public:
    bool decode(Buffer&) noexcept override;
    std::string description(const std::string&) const noexcept override;

private:
    int32_t getDescrLen(Buffer&) noexcept;
};

class BoxMeta : public Box
{
public:
    uint8_t version;
    uint32_t flags;
public:
    BoxMeta(BoxInfo&& boxInfo)
        : Box(std::move(boxInfo)) {
        
    }
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
    BoxAvcc(BoxInfo&& boxInfo)
        : Box(std::move(boxInfo)) {
        
    }
    ~BoxAvcc() override = default;

public:
    bool decode(Buffer&) noexcept override;
    std::string description(const std::string&) const noexcept override;
};

class BoxHvcc : public Box {
public:
    uint8_t version;
    uint8_t profileSpace;
    uint8_t tierFlag;
    uint8_t profileIdc;
    uint32_t profileCompatibility;
    uint8_t levelIdc;
    uint16_t avgFrameRate;
    uint16_t naluByteSize;
    
    std::vector<DataRefPtr> sps;
    std::vector<DataRefPtr> pps;
    std::vector<DataRefPtr> vps;
public:
    BoxHvcc(BoxInfo&& boxInfo)
        : Box(std::move(boxInfo)) {
        
    }
    ~BoxHvcc() override = default;

public:
    bool decode(Buffer&) noexcept override;
    std::string description(const std::string&) const noexcept override;
};

}
