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
    info.start = buffer.pos();
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
        {"esds", [](BoxInfo&& info) { return new BoxEsds(std::move(info)); }},
        {"meta", [](BoxInfo&& info) { return new BoxMeta(std::move(info)); }},
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
                            "{1}  minor version: {2}\n",
                            "{1}  compatible brands: {3}",
                            prefix, majorBrand, minorVersion, compatibleBrands));
    return desc;
}

bool BoxFtyp::decode(Buffer& buffer) noexcept {
    int32_t offset;
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
    for (int i = 0; i < matrix.size(); i++) {
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
        "{0}    quality: {8}",
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
        } else if (box->info.symbol == "avc1" || box->info.symbol == "hev1" || box->info.symbol == "vp09") {
            buffer.skip(6);
            buffer.read2ByteBE(dataReferenceIndex);
            buffer.skip(2 + 2 + 3 * 4);
            buffer.read2ByteBE(width);
            buffer.read2ByteBE(height);
            buffer.skip(12);
            buffer.read2ByteBE(frameCount);
            buffer.skip(32 + 2 + 2);
        }
        auto subBox = Box::createBox(buffer);
        if (subBox) {
            box->append(subBox);
        }
        buffer.skip(endPos - buffer.pos());
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
        if (!buffer.require(8)) {
            res = false;
            break;
        }
        SttsEntry entry;
        buffer.read4ByteBE(entry.sampleCount);
        buffer.read4ByteBE(entry.sampleDuration);
        entrys.push_back(entry);
        offset += 8;
    }
    if (!res) {
        buffer.skip(-offset);
    }
    return res;
}

std::string BoxEsds::description(const std::string& prefix) const noexcept {
    return Box::description(prefix) + " codec." + std::format("{:#x}", objectTypeId);
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

}
