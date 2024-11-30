//
//  Mp4Box.cpp
//  slark
//
//  Created by Nevermore
//

#include <format>
#include "Mp4Box.hpp"
#include "Util.hpp"

namespace slark {

std::expected<BoxInfo, bool> Box::tryParseBoxInfo(Buffer& buffer) noexcept {
    if (!buffer.require(8)) {
        return std::unexpected(false);
    }
    BoxInfo info;
    info.start = static_cast<uint32_t>(buffer.pos());
    uint32_t tmpSize = 0;
    if (!buffer.read4ByteBE(tmpSize)) {
        return std::unexpected(false);
    }
    
    if (!buffer.read4ByteBE(info.type)) {
        buffer.skip(-4);
        return std::unexpected(false);
    }
    
    info.size = tmpSize;
    if (info.size == 1) {
        if (!buffer.read8ByteBE(info.size)) {
            buffer.skip(-8);
            return std::unexpected(false);
        } else {
            info.headerSize += 8;
        }
    } else if (info.size == 0) {
        info.size = buffer.totalSize() - buffer.pos() + info.headerSize;
    }
    info.symbol = Util::uint32ToByteBE(info.type);
    return info;
}

std::string Box::description(const std::string& prefix) const noexcept {
    return std::format("{} {} [{}, {}) \n", prefix, info.symbol, info.start, info.start + info.size);
}

void Box::append(std::shared_ptr<Box> child) noexcept {
    childs.push_back(std::move(child));
}

BoxRefPtr Box::getChild(std::string_view childType) const noexcept {
    for (const auto& child : childs) {
        if (child->info.symbol== childType) {
            return child;
        }
    }
    return nullptr;
}

bool Box::decode(Buffer&) noexcept {
    return true;
}

BoxRefPtr Box::createBox(Buffer& buffer) {
    auto info = tryParseBoxInfo(buffer);
    if (!info.has_value()) {
        return nullptr;
    }
    auto boxInfo = info.value();
    static const std::unordered_map<std::string, std::function<Box*(BoxInfo&&)>> boxFactory = {
        {"ftyp", [](BoxInfo&& info) { return new BoxFtyp(std::move(info)); }},
        {"mvhd", [](BoxInfo&& info) { return new BoxMvhd(std::move(info)); }},
        {"stsd", [](BoxInfo&& info) { return new BoxStsd(std::move(info)); }},
        {"stco", [](BoxInfo&& info) { return new BoxStco(std::move(info)); }},
        {"stsz", [](BoxInfo&& info) { return new BoxStsz(std::move(info)); }},
        {"stsc", [](BoxInfo&& info) { return new BoxStsc(std::move(info)); }},
        {"stts", [](BoxInfo&& info) { return new BoxStts(std::move(info)); }},
        {"ctts", [](BoxInfo&& info) { return new BoxCtts(std::move(info)); }},
        {"stss", [](BoxInfo&& info) { return new BoxStss(std::move(info)); }},
        {"esds", [](BoxInfo&& info) { return new BoxEsds(std::move(info)); }},
        {"meta", [](BoxInfo&& info) { return new BoxMeta(std::move(info)); }},
        {"avcC", [](BoxInfo&& info) { return new BoxAvcc(std::move(info)); }},
        {"hvcC", [](BoxInfo&& info) { return new BoxHvcc(std::move(info)); }},
        {"mdhd", [](BoxInfo&& info) { return new BoxMdhd(std::move(info)); }}
    };
    if (boxFactory.contains(boxInfo.symbol)) {
        auto box = boxFactory.at(boxInfo.symbol)(std::move(boxInfo));
        return std::shared_ptr<Box>(box);
    }

    return std::make_shared<Box>(std::move(boxInfo));
}

std::string BoxFtyp::description(const std::string& prefix) const noexcept {
    auto desc = Box::description(prefix);
    desc.append(std::format("{1}  major brand: {1}\n"
                            "{1}  minor version: {2}\n"
                            "{1}  compatible brands: {3}\n",
                            prefix, majorBrand, minorVersion, compatibleBrands));
    return desc;
}

bool BoxFtyp::decode(Buffer& buffer) noexcept {
    int32_t offset = 0;
    bool res = false;
    do {
        if (!buffer.readString(4, majorBrand)) {
            break;
        } else {
            offset += 4;
        }
        
        if (!buffer.readString(4, majorBrand)) {
            break;
        } else {
            offset += 4;
        }
        
        auto size = info.size - buffer.pos() - info.start;
        if (!buffer.readString(size, compatibleBrands)) {
            break;
        } else {
            res = true;
        }
    } while(0);
    if (!res) {
        buffer.skip(-offset);
    }
    return res;
}


std::string BoxMvhd::matrixString(const std::string& prefix) const noexcept {
    std::string res;
    static const std::vector<std::string> matrixNames = {"a, b, u", "c, d, v", "x, y, w"};
    
    for (size_t i = 0; i < matrixNames.size(); ++i) {
        res += std::format("{}| {} |{}| {}, {}, {} |{}",
                           prefix, matrixNames[i], (i != 1 ? "   " : " = "),
                           matrix[i * 3], matrix[i * 3 + 1], matrix[i * 3 + 2],
                           (i != matrixNames.size() - 1 ? "\n" : ""));
    }
    
    return res;
}

std::string BoxMvhd::description(const std::string& prefix) const noexcept {
    std::string res = Box::description(prefix);
    
    res += std::format(
        "{0}    version: {1}\n"
        "{0}    flags: {2}\n"
        "{0}    creationTime: {3}\n"
        "{0}    modificationTime: {4}\n"
        "{0}    timeScale: {5}\n"
        "{0}    duration: {6}\n"
        "{0}    preferredRate: {7}\n"
        "{0}    preferredVolume: {8}\n"
        "{0}    matrix: \n {9} \n"
        "{0}    nextTrackId: {10}",
        prefix,
        version, flags, creationTime,
        modificationTime, timeScale, duration,
        preferredRate, preferredVolume, matrixString(prefix + "        "),
        nextTrackId
    );

    return res;
}

bool BoxMvhd::decode(Buffer& buffer) noexcept {
    if (!buffer.require(100)) {
        return false;
    }
    buffer.readByte(version);
    buffer.read3ByteBE(flags);
    buffer.read4ByteBE(creationTime);
    buffer.read4ByteBE(modificationTime);
    buffer.read4ByteBE(timeScale);
    buffer.read4ByteBE(duration);
    uint16_t hRate = 0, lRate = 0;
    buffer.read2ByteBE(hRate);
    buffer.read2ByteBE(lRate);
    preferredRate = static_cast<float>(hRate) + static_cast<float>(lRate) / 65535.0f;
    uint8_t hVolume = 0, lVolume = 0;
    buffer.readByte(hVolume);
    buffer.readByte(lVolume);
    preferredVolume = static_cast<float>(hVolume) + static_cast<float>(lVolume) / 255.0f;
    buffer.readString(10, reserved);
    for (size_t i = 0; i < matrix.size(); i++) {
        uint32_t v = 0;
        buffer.read4ByteBE(v);
        if ((i + 1) % 3 == 0) {
            auto h = (v >> 30) & 0x03;
            auto l = v & 0x3fffffff;
            matrix[i] = static_cast<float>(h) + static_cast<float>(l) / 1073741824.f;
        } else {
            auto h = (v >> 16) & 0xffff;
            auto l = v & 0xffff;
            matrix[i] = static_cast<float>(h) + static_cast<float>(l) / 65535.f;
        }
    }
    buffer.read4ByteBE(previewTime);
    buffer.read4ByteBE(previewDuration);
    buffer.read4ByteBE(posterTime);
    buffer.read4ByteBE(selectionTime);
    buffer.read4ByteBE(selectionDuration);
    buffer.read4ByteBE(currentTime);
    buffer.read4ByteBE(nextTrackId);
    return true;
}

std::string BoxMdhd::description(const std::string& prefix) const noexcept {
    auto res = Box::description(prefix);
    res += std::format(
        "{0}    version: {1}\n"
        "{0}    flags: {2}\n"
        "{0}    creation_time: {3}\n"
        "{0}    modification_time: {4}\n"
        "{0}    time_scale: {5}\n"
        "{0}    duration: {6}\n"
        "{0}    language: {7}\n"
        "{0}    quality: {8}\n",
        prefix, version, flags,
        creationTime, modificationTime, timeScale,
        duration, language, quality
    );
    return res;
}

bool BoxMdhd::decode(Buffer& buffer) noexcept {
    if (!buffer.require(36)) {
        return false;
    }
    buffer.readByte(version);
    buffer.read3ByteBE(flags);
    if (version == 0) {
        uint32_t tCreationTime;
        buffer.read4ByteBE(tCreationTime);
        creationTime = static_cast<uint64_t>(tCreationTime);
        
        uint32_t tModificationTime;
        buffer.read4ByteBE(tModificationTime);
        modificationTime = static_cast<uint64_t>(tModificationTime);
        
        buffer.read4ByteBE(timeScale);
        
        uint32_t tDuration;
        buffer.read4ByteBE(tDuration);
        duration = static_cast<uint64_t>(tDuration);
    } else {
        buffer.read8ByteBE(creationTime);
        buffer.read8ByteBE(modificationTime);
        buffer.read4ByteBE(timeScale);
        buffer.read8ByteBE(duration);
    }
    buffer.read2ByteBE(language);
    buffer.read2ByteBE(quality);
    return true;
}

CodecId BoxStsd::getCodecId() {
    using namespace std::string_view_literals;
    for (auto& ptr : childs) {
        if (auto box = ptr->getChild("esds"sv); box) {
            auto esds = std::dynamic_pointer_cast<BoxEsds>(box);
            return esds->codecId;
        } else if (ptr->info.symbol == "hev1") {
            return CodecId::HEVC;
        } else if (ptr->info.symbol == "avc1") {
            return CodecId::AVC;
        } else if (ptr->info.symbol == "Opus") {
            return CodecId::OPUS;
        } else if (ptr->info.symbol == "vp09") {
            return CodecId::VP9;
        }
    }
    return CodecId::Unknown;
}

//stsd
//  ├── size: 56
//  ├── type: 'stsd'
//  ├── version_and_flags: 0x000000
//  ├── entry_count: 2
//  ├── sample_entries:
//  │   ├── [0]: mp4a (音频样本描述)
//  │   │   ├── size: 32
//  │   │   ├── type: 'mp4a'
//  │   │   ├── version_and_flags: 0x000000
//  │   │   ├── data_reference_index: 0
//  │   │   ├── version: 0
//  │   │   ├── channels: 2
//  │   │   ├── sample_size: 16
//  │   │   ├── sample_rate: 44100
//  │   │   ├── reserved: 0
//  │   │   ├── avg_bytes_per_second: 128000
//  │   │   ├── extra_data_size: 6
//  │   │   └── extra_data: { 0x13, 0x10, 0x00, 0x00, 0x02, 0x00 }
//  │   └── [1]: avc1 (视频样本描述)
//  │       ├── size: 40
//  │       ├── type: 'avc1'
//  │       ├── version_and_flags: 0x000000
//  │       ├── data_reference_index: 0
//  │       ├── width: 1920
//  │       ├── height: 1080
//  │       ├── horizresolution: 72
//  │       ├── vertresolution: 72
//  │       ├── frame_count: 1
//  │       ├── compressorname: 'AVC Coding'
//  │       ├── depth: 24
//  │       └── pre_defined: 0

bool BoxStsd::decode(Buffer& buffer) noexcept {
    if (!buffer.require(110)) {
        return false;
    }
    buffer.readByte(version);
    buffer.read3ByteBE(flags);
    buffer.read4ByteBE(entryCount);
    
    for (uint32_t i = 0; i < entryCount; i++) {
        auto box = Box::createBox(buffer);
        append(box);
        auto endPos = box->info.start + box->info.size;
        if (box->info.symbol == "mp4a" || box->info.symbol == "Opus") {
            buffer.skip(6);
            buffer.read2ByteBE(dataReferenceIndex);
            buffer.skip(2 + 2 + 4);
            buffer.read2ByteBE(channelCount);
            buffer.read2ByteBE(sampleSize);
            buffer.read2ByteBE(audioCompressionId);
            buffer.read2ByteBE(packetSize);
            uint32_t tSampleRate;
            buffer.read4ByteBE(tSampleRate);
            sampleRate = static_cast<uint16_t>(tSampleRate >> 16);
            isAudio = true;
        } else if (box->info.symbol == "avc1" || box->info.symbol == "hev1" || box->info.symbol == "vp09") {
            buffer.skip(6);
            buffer.read2ByteBE(dataReferenceIndex);
            buffer.skip(2 + 2 + 3 * 4);
            buffer.read2ByteBE(width);
            buffer.read2ByteBE(height);
            buffer.skip(12);
            buffer.read2ByteBE(frameCount);
            buffer.skip(32 + 2 + 2);
            isVideo = true;
        }
        auto subBox = Box::createBox(buffer);
        if (subBox) {
            box->append(subBox);
            subBox->decode(buffer);
        }
        buffer.skip(static_cast<int64_t>(endPos - buffer.pos()));
    }
    return true;
}

bool BoxStsc::decode(Buffer& buffer) noexcept {
    if (!buffer.require(1 + 3 + 4 + 3 * 4)) {
        return false;
    }
    int32_t offset = 0;
    buffer.skip(1 + 3);
    offset += 4;
    
    uint32_t entryCount = 0;
    buffer.read4ByteBE(entryCount);
    offset += 4;
    
    bool res = true;
    for (uint32_t i = 0; i < entryCount; i++) {
        if (!buffer.require(12)) {
            res = false;
            break;
        }
        StscEntry entry;
        buffer.read4ByteBE(entry.firstChunk);
        buffer.read4ByteBE(entry.samplesPerChunk);
        buffer.read4ByteBE(entry.sampleDescriptionIndex);
        offset += 12;
        entrys.push_back(entry);
    }
    if (!res) {
        buffer.skip(-offset);
    }
    return res;
}

bool BoxStsz::decode(Buffer& buffer) noexcept {
    if (!buffer.require(12)) {
        return false;
    }
    
    int32_t offset = 0;
    buffer.skip(1 + 3); //version + flags
    offset += 4;
    
    uint32_t sampleSize = 0;
    buffer.read4ByteBE(sampleSize);
    offset += 4;
    
    uint32_t entryCount = 0;
    buffer.read4ByteBE(entryCount);
    offset += 4;
    
    if (sampleSize != 0) {
        sampleSizes = std::vector<uint64_t>(entryCount, sampleSize);
    } else {
        bool res = true;
        for (uint32_t i = 0; i < entryCount; i++) {
            uint32_t size = 0;
            if (!buffer.read4ByteBE(size)) {
                res = false;
                break;
            }
            offset += 4;
            sampleSizes.push_back(size);
        }
        if (!res) {
            buffer.skip(-offset);
        }
        return res;
    }
    return true;
}

bool BoxStco::decode(Buffer& buffer) noexcept {
    if (!buffer.require(12)) {
        return false;
    }
    
    int32_t offset = 0;
    buffer.skip(1 + 3); //version + flags
    offset += 4;
    
    uint32_t entryCount = 0;
    buffer.read4ByteBE(entryCount);
    offset += 4;
    
    bool res = true;
    for (uint32_t i = 0; i < entryCount; i++) {
        uint32_t size = 0;
        if (!buffer.read4ByteBE(size)) {
            res = false;
            break;
        }
        offset += 4;
        chunkOffsets.push_back(size);
    }
    if (!res) {
        buffer.skip(-offset);
    }
    return res;
}

bool BoxStts::decode(Buffer& buffer) noexcept {
    buffer.skip(1 + 3); //version + flags
    
    uint32_t entryCount = 0;
    buffer.read4ByteBE(entryCount);
    
    bool res = true;
    for (uint32_t i = 0; i < entryCount; i++) {
        SttsEntry entry;
        buffer.read4ByteBE(entry.sampleCount);
        buffer.read4ByteBE(entry.sampleDelta);
        entrys.push_back(entry);
    }
    return res;
}

uint32_t BoxStts::sampleSize() const noexcept {
    uint32_t count = 0;
    for (auto& entry : entrys) {
        count += entry.sampleCount;
    }
    return count;
}

bool BoxCtts::decode(Buffer& buffer) noexcept {
    buffer.skip(1 + 3); //version + flags
    
    uint32_t entryCount = 0;
    buffer.read4ByteBE(entryCount);

    for (size_t i = 0; i < entryCount; i++) {
        CttsEntry entry;
        buffer.read4ByteBE(entry.sampleCount);
        buffer.read4ByteBE(entry.sampleOffset);
        entrys.push_back(entry);
    }
    return true;
}

bool BoxStss::decode(Buffer& buffer) noexcept {
    buffer.skip(1 + 3); //version + flags
    uint32_t size = 0;
    buffer.read4ByteBE(size);
    for (size_t i = 0; i < size; i++) {
        uint32_t index = 0;
        buffer.read4ByteBE(index);
        keyIndexs.push_back(index);
    }
    return true;
}

std::string BoxEsds::description(const std::string& prefix) const noexcept {
    auto desc = Box::description(prefix);
    desc.pop_back();
    return desc + " codec:" + std::format("{:#x}\n", objectTypeId);
}

bool BoxEsds::decode(Buffer& buffer) noexcept {
    if (!buffer.require(256)) {
        return false;
    }
    buffer.skip(1 + 3); //version + flags
    
    uint8_t esDescTag = 0;
    buffer.readByte(esDescTag);
    
    auto descLen = getDescrLen(buffer);
    
    if (esDescTag == 0x03) {
        buffer.skip(2);
        
        uint8_t flags = 0;
        buffer.readByte(flags);
        if (flags & 0x80) {
            buffer.skip(2);
        }
        
        if (flags & 0x40) {
            uint8_t len = 0;
            buffer.readByte(len);
            buffer.skip(len);
        }
        
        if (flags & 0x20) {
            buffer.skip(2);
        }
    } else {
        buffer.skip(2);
    }
    
    buffer.readByte(esDescTag);
    descLen = getDescrLen(buffer);
    
    if (esDescTag == 0x04) {
        buffer.readByte(objectTypeId);
        buffer.skip(1);
        buffer.read3ByteBE(bufferSize);
        buffer.read4ByteBE(maxBitRate);
        buffer.read4ByteBE(avgBitRate);
        
        buffer.readByte(esDescTag);
        descLen = getDescrLen(buffer);
        if (objectTypeId == 0x40) {
            codecId = CodecId::AAC;
            if (esDescTag == 0x05) {
                uint16_t value = 0;
                buffer.read2ByteBE(value);
                audioSpecConfig.audioObjectType = (value >> 11) & 0x1f;
                audioSpecConfig.samplingFrequencyIndex = (value >> 7) & 0xf;
                audioSpecConfig.channelConfiguration = (value >> 3) & 0xf;
            }
        } else if (objectTypeId == 0x6b) {
            codecId = CodecId::MP3;
        }
    }
    return true;
}

int32_t BoxEsds::getDescrLen(Buffer& buffer) noexcept {
    int32_t len = 0;
    int32_t count = 4;
    while (count--) {
        uint8_t c;
        buffer.readByte(c);
        len = (len << 7) | (c & 0x7f);
        if (!(c & 0x80)) {
            break;
        }
    }
    return len;
}

bool BoxMeta::decode(Buffer& buffer) noexcept {
    if (!buffer.require(4)) {
        return false;
    }
    buffer.readByte(version);
    buffer.read3ByteBE(flags);
    return true;
}

std::string BoxAvcc::description(const std::string&prefix) const noexcept {
    auto desc = Box::description(prefix);
    desc.pop_back();
    return desc +
        std::format("version: {}, profileIndication: {}, "
                "profileCompatibility: {}, levelIndication: {},"
                "naluByteSize: {}\n",
                version, profileIndication,
                profileCompatibility, levelIndication,
                naluByteSize);
}

//avcC (size)
//  |--- (size) 1字节: configurationVersion
//  |--- (size) 1字节: AVCProfileIndication
//  |--- (size) 1字节: profile_compatibility
//  |--- (size) 1字节: AVCLevelIndication
//  |--- (size) 6位 reserved + 2位 lengthSizeMinusOne
//  |--- (size) 3位 reserved + 5位 numOfSPS
//  |--- (size) 2字节: SPS length
//  |--- (size) N字节: SPS data
//  |--- (size) 1字节: numOfPPS
//  |--- (size) 2字节: PPS length
//  |--- (size) N字节: PPS data
bool BoxAvcc::decode(Buffer& buffer) noexcept {
    auto endPos = info.size + info.start;
    buffer.readByte(version);
    buffer.readByte(profileIndication);
    buffer.readByte(profileCompatibility);
    buffer.readByte(levelIndication);
    buffer.readByte(naluByteSize);
    naluByteSize = naluByteSize & 0x03;
    uint8_t spsCount = 0;
    buffer.readByte(spsCount);
    spsCount = spsCount & 0x1f;
    for (uint8_t i = 0; i < spsCount; i++) {
        uint16_t spsLength = 0;
        buffer.read2ByteBE(spsLength);
        auto data = buffer.readData(spsLength);
        sps.push_back(std::move(data));
    }
    uint8_t ppsCount = 0;
    buffer.readByte(ppsCount);
    ppsCount = ppsCount & 0x1f;
    for (uint8_t i = 0; i < ppsCount; i++) {
        uint16_t ppsLength = 0;
        buffer.read2ByteBE(ppsLength);
        auto data = buffer.readData(ppsLength);
        pps.push_back(std::move(data));
    }
    return true;
}

std::string BoxHvcc::description(const std::string&prefix) const noexcept {
    return Box::description(prefix) +
        std::format("version: {}, profileSpace: {}, "
                "tierFlag: {}, profileIdc: {},"
                "profileCompatibility: {} , levelIdc:{}, naluByteSize:{}\n",
                version, profileSpace,
                tierFlag, profileIdc,
                profileCompatibility, levelIdc, naluByteSize);
}

//hvcC (size)
//  |--- (1字节) configurationVersion: 配置版本，通常为 1
//  |--- (1字节) general_profile_space
//  |--- (4字节) general_profile_compatibility_flags
//  |--- (6字节) general_constraint_indicator_flags
//  |--- (1字节) general_level_idc
//  |--- (2字节) min_spatial_segmentation_idc
//  |--- (1字节) parallelismType
//  |--- (1字节) chromaFormat
//  |--- (1字节) bitDepthLumaMinus8
//  |--- (1字节) bitDepthChromaMinus8
//  |--- (2字节) avgFrameRate
//  |--- (1字节) constantFrameRate, numTemporalLayers, temporalIdNested, lengthSizeMinusOne
//  |--- (1字节) numOfArrays: NALU 数组的数量
//       |--- Array 1: (1字节) NAL unit type
//                    (2字节) numNalus
//                    (2字节) nalUnitLength
//                    NALU data (如 VPS, SPS, PPS)
//       |--- Array 2: (类似 Array 1)
//       |--- ...
bool BoxHvcc::decode(Buffer& buffer) noexcept {
    auto endPos = info.size + info.start;
    buffer.readByte(version);
    buffer.readByte(profileSpace);
    buffer.read4ByteBE(profileCompatibility);
    buffer.skip(6);
    buffer.readByte(levelIdc);
    buffer.skip(2 + 1 + 1 + 1 + 1);
    buffer.read2ByteBE(avgFrameRate);
    uint8_t value = 0;
    buffer.readByte(value);
    naluByteSize = value & 0x1f;
    uint8_t naluArraySize = 0;
    buffer.readByte(naluArraySize);
    for (uint8_t i = 0; i < naluArraySize; i++) {
        uint8_t naluType = 0;
        buffer.readByte(naluType);
        std::vector<DataRefPtr> vec;
        uint16_t naluCount = 0;
        buffer.read2ByteBE(naluCount);
        uint16_t naluSize = 0;
        buffer.read2ByteBE(naluSize);
        for (uint16_t j = 0; j < naluCount; j++) {
            auto data = buffer.readData(naluSize);
            vec.push_back(std::move(data));
        }
        if (naluType == 32) {
            vps = std::move(vec);
        } else if (naluType == 33) {
            sps = std::move(vec);
        } else if (naluType == 34) {
            pps = std::move(vec);
        }
    }
    auto skip = static_cast<int64_t>(endPos - buffer.readPos());
    buffer.skip(skip);
    return true;
}

}
