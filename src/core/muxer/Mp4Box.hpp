//
//  Mp4Box.hpp
//  slark
//
//  Created by Nevermore
//

#include "Data.hpp"

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

class Box
{
public:
    uint32_t start = 0;
    uint32_t type = 0;
    uint64_t size = 0;
    uint32_t headerSize = 8;
    std::vector<BoxRefPtr> childs;
public:
    Box() = default;
    virtual ~Box() = default;
    
public:
    virtual bool decode(std::string_view);
    [[nodiscard]] virtual std::string description(const std::string&) const noexcept; //debug
    
    void append(std::shared_ptr<Box> child) noexcept;
    [[nodiscard]] virtual BoxRefPtr getChild(std::string_view type) const noexcept;
    
public:
    static BoxRefPtr createBox(std::string_view sv);
};

class BoxFtyp : public Box
{
public:
    std::string majorBrand;
    uint32_t minorVersion;
    std::string compatibleBrands;
public:
    BoxFtyp() = default;
    ~BoxFtyp() = default;

public:
    bool decode(std::string_view) override;
    std::string description(const std::string& ) const noexcept override;
};

class BoxMvhd : public Box
{
public:
    uint8_t version;
    uint32_t flags;
    uint32_t creationTime;
    uint32_t modificationTime;
    uint32_t timeScale;
    uint32_t duration;
    float preferredRate;
    float preferredVolume;
    std::string reserved; // 10 bytes
    std::vector<float> matrix; // 36 bytes
    uint32_t previewTime;
    uint32_t previewDuration;
    uint32_t posterTime;
    uint32_t selectionTime;
    uint32_t selectionDuration;
    uint32_t currentTime;
    uint32_t nextTrackId;
public:
    BoxMvhd() = default;
    ~BoxMvhd() override = default;

public:
    bool decode(std::string_view) override;
    std::string description(const std::string&) const noexcept override;

private:
    std::string matrixString(const std::string&);
};

class BoxMdhd : public Box
{
public:
    uint8_t version;
    uint32_t flags;
    uint32_t creationTime;
    uint32_t modificationTime;
    uint32_t timeScale;
    uint32_t duration;
    uint16_t language;
    uint16_t quality;
public:
    BoxMdhd() = default;
    ~BoxMdhd() override = default;

public:
    bool decode(std::string_view) override;
    std::string description(const std::string&) const noexcept override;
};

struct AudioSpecificConfig {
    uint8_t extensionFlag : 1;
    uint8_t dependsOnCoreCoder : 1;
    uint8_t frameLengthFlag : 1;
    uint8_t channelConfiguration : 4;
    uint8_t samplingFrequencyIndex : 4;
    uint8_t audioObjectType : 5;
};

class BoxStsd : public Box
{
public:
    uint8_t version;
    uint32_t flags;
    uint32_t entries_count;
public:
    BoxStsd() = default;
    ~BoxStsd() override = default;

    virtual CodecId getCodecId();

public:
    bool decode(std::string_view) override;
};

struct StscEntry
{
    uint32_t firstChunk;
    uint32_t samplesPerChunk;
    uint32_t sampleId;
};

class BoxStsc : public Box
{
public:
    std::vector<StscEntry> entries;
public:
    BoxStsc() = default;
    ~BoxStsc() override = default;

public:
    bool decode(std::string_view) override;
};

class BoxStsz : public Box
{
public:
    std::vector<uint64_t> sample_sizes;
public:
    BoxStsz() = default;
    ~BoxStsz() override = default;

public:
    bool decode(std::string_view) override;
};

class BoxStco : public Box
{
public:
    std::vector<uint64_t> chunk_offsets;
public:
    BoxStco() = default;
    ~BoxStco() override = default;

public:
    bool decode(std::string_view) override;
};

struct SttsEntry
{
    uint32_t sampleCount;
    uint32_t sampleDuration;
};

class BoxStts : public Box
{
public:
    std::vector<SttsEntry> entries;
public:
    BoxStts() = default;
    ~BoxStts() override = default;

public:
    bool decode(std::string_view) override;
};

class BoxEsds : public Box
{
public:
    int8_t objectTypeId;
    CodecId codecId;
    AudioSpecificConfig audioSpecConfig;
public:
    BoxEsds() = default;
    ~BoxEsds() override = default;

public:
    bool decode(std::string_view) override;
    std::string description(const std::string&) const noexcept override;

private:
    int getDescrLen(std::string_view);
};

class BoxMeta : public Box
{
public:
    uint8_t version;
    uint32_t flags;
public:
    BoxMeta() = default;
    ~BoxMeta() override = default;

public:
    bool decode(std::string_view) override;
};

}
