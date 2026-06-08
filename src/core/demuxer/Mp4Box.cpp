//
//  Mp4Box.cpp
//  slark
//
//  Created by Nevermore
//

#include "Mp4Box.h"

#include "Log.hpp"
#include "Util.h"

#include <format>

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
    info.symbol = Util::toStringBE<uint32_t>(info.type);
    // 警告但不拒绝：非可打印 ASCII 的 box type 可能是垃圾数据，仍尝试跳过
    for (auto c : info.symbol) {
        if (c < 0x20 || c > 0x7E) {
            LogW("tryParseBoxInfo: invalid box type '{}' at pos:{}", info.symbol, info.start);
            break;
        }
    }
    return info;
}

std::string Box::description(const std::string& prefix) const noexcept {
    return std::format("{} {} [{}, {}) \n",
                       prefix,
                       info.symbol,
                       info.start,
                       info.start + info.size);
}

void Box::append(std::shared_ptr<Box> child) noexcept {
    childs.push_back(std::move(child));
}

BoxRefPtr Box::getChild(std::string_view childType) const noexcept {
    for (const auto& child : childs) {
        if (child->info.symbol == childType) {
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
    static const std::unordered_map<std::string, std::function<Box*(BoxInfo&&)>> kBoxFactory = {
        {"ftyp",
         [](BoxInfo&& info) {
             return new BoxFtyp(std::move(info));
         }},
        {"mvhd",
         [](BoxInfo&& info) {
             return new BoxMvhd(std::move(info));
         }},
        {"stsd",
         [](BoxInfo&& info) {
             return new BoxStsd(std::move(info));
         }},
        {"stco",
         [](BoxInfo&& info) {
             return new BoxStco(std::move(info));
         }},
        {"stsz",
         [](BoxInfo&& info) {
             return new BoxStsz(std::move(info));
         }},
        {"stsc",
         [](BoxInfo&& info) {
             return new BoxStsc(std::move(info));
         }},
        {"stts",
         [](BoxInfo&& info) {
             return new BoxStts(std::move(info));
         }},
        {"ctts",
         [](BoxInfo&& info) {
             return new BoxCtts(std::move(info));
         }},
        {"stss",
         [](BoxInfo&& info) {
             return new BoxStss(std::move(info));
         }},
        {"esds",
         [](BoxInfo&& info) {
             return new BoxEsds(std::move(info));
         }},
        {"meta",
         [](BoxInfo&& info) {
             return new BoxMeta(std::move(info));
         }},
        {"avcC",
         [](BoxInfo&& info) {
             return new BoxAvcc(std::move(info));
         }},
        {"hvcC",
         [](BoxInfo&& info) {
             return new BoxHvcc(std::move(info));
         }},
        {"co64",
         [](BoxInfo&& info) {
             return new BoxCo64(std::move(info));
         }},
        {"mdhd",
         [](BoxInfo&& info) {
             return new BoxMdhd(std::move(info));
         }},
        {"elst",
         [](BoxInfo&& info) {
             return new BoxElst(std::move(info));
         }},
        {"mvex",
         [](BoxInfo&& info) {
             return new BoxMvex(std::move(info));
         }},
        {"trex",
         [](BoxInfo&& info) {
             return new BoxTrex(std::move(info));
         }},
        {"moof",
         [](BoxInfo&& info) {
             return new BoxMoof(std::move(info));
         }},
        {"mfhd",
         [](BoxInfo&& info) {
             return new BoxMfhd(std::move(info));
         }},
        {"traf",
         [](BoxInfo&& info) {
             return new BoxTraf(std::move(info));
         }},
        {"tfhd",
         [](BoxInfo&& info) {
             return new BoxTfhd(std::move(info));
         }},
        {"tfdt",
         [](BoxInfo&& info) {
             return new BoxTfdt(std::move(info));
         }},
        {"trun",
         [](BoxInfo&& info) {
             return new BoxTrun(std::move(info));
         }},
        {"tkhd",
         [](BoxInfo&& info) {
             return new BoxTkhd(std::move(info));
         }},
        {"hdlr",
         [](BoxInfo&& info) {
             return new BoxHdlr(std::move(info));
         }},
        {"vmhd",
         [](BoxInfo&& info) {
             return new BoxVmhd(std::move(info));
         }},
        {"smhd",
         [](BoxInfo&& info) {
             return new BoxSmhd(std::move(info));
         }},
        {"dref",
         [](BoxInfo&& info) {
             return new BoxDref(std::move(info));
         }},
        {"sidx", [](BoxInfo&& info) {
             return new BoxSidx(std::move(info));
         }}};
    if (kBoxFactory.contains(boxInfo.symbol)) {
        auto box = kBoxFactory.at(boxInfo.symbol)(std::move(boxInfo));
        return std::shared_ptr<Box>(box);
    }

    return std::make_shared<Box>(std::move(boxInfo));
}

std::string BoxFtyp::description(const std::string& prefix) const noexcept {
    auto desc = Box::description(prefix);
    desc.append(std::format("{}  major brand: {}\n"
                            "{}  minor version: {}\n"
                            "{}  compatible brands: {}\n",
                            prefix,
                            majorBrand,
                            prefix,
                            minorVersion,
                            prefix,
                            compatibleBrands));
    return desc;
}

bool BoxFtyp::decode(Buffer& buffer) noexcept {
    if (!buffer.require(8)) {
        return false;
    }
    if (!buffer.readString(4, majorBrand)) {
        return false;
    }
    if (!buffer.read4ByteBE(minorVersion)) {
        return false;
    }
    const auto remaining =
        info.size > info.headerSize + 8 ? info.size - info.headerSize - 8 : uint64_t{0};
    if (remaining > 0) {
        buffer.readString(static_cast<uint32_t>(remaining), compatibleBrands);
    }
    return true;
}

std::string BoxMvhd::matrixString(const std::string& prefix) const noexcept {
    std::string res;
    static const std::vector<std::string> kMatrixNames = {"a, b, u", "c, d, v", "x, y, w"};

    for (size_t i = 0; i < kMatrixNames.size(); ++i) {
        res += std::format("{}| {} |{}| {}, {}, {} |{}",
                           prefix,
                           kMatrixNames[i],
                           (i != 1 ? "   " : " = "),
                           matrix[i * 3],
                           matrix[i * 3 + 1],
                           matrix[i * 3 + 2],
                           (i != kMatrixNames.size() - 1 ? "\n" : ""));
    }

    return res;
}

std::string BoxMvhd::description(const std::string& prefix) const noexcept {
    std::string res = Box::description(prefix);

    res += std::format("{0}    version: {1}\n"
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
                       version,
                       flags,
                       creationTime,
                       modificationTime,
                       timeScale,
                       duration,
                       preferredRate,
                       preferredVolume,
                       matrixString(prefix + "        "),
                       nextTrackId);

    return res;
}

bool BoxMvhd::decode(Buffer& buffer) noexcept {
    if (!buffer.require(4)) {
        return false;
    }
    buffer.readByte(version);
    buffer.read3ByteBE(flags);
    if (version == 1) {
        buffer.read8ByteBE(creationTime);
        buffer.read8ByteBE(modificationTime);
        buffer.read4ByteBE(timeScale);
        buffer.read8ByteBE(duration);
    } else {
        uint32_t tmp = 0;
        buffer.read4ByteBE(tmp);
        creationTime = tmp;
        buffer.read4ByteBE(tmp);
        modificationTime = tmp;
        buffer.read4ByteBE(timeScale);
        buffer.read4ByteBE(tmp);
        duration = tmp;
    }
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
    res += std::format("{0}    version: {1}\n"
                       "{0}    flags: {2}\n"
                       "{0}    creation_time: {3}\n"
                       "{0}    modification_time: {4}\n"
                       "{0}    time_scale: {5}\n"
                       "{0}    duration: {6}\n"
                       "{0}    language: {7}\n"
                       "{0}    quality: {8}\n",
                       prefix,
                       version,
                       flags,
                       creationTime,
                       modificationTime,
                       timeScale,
                       duration,
                       language,
                       quality);
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

std::tuple<CodecId, std::string> BoxStsd::getCodecId() {
    using namespace std::string_view_literals;
    LogI("getCodecId: childs={}", childs.size());
    for (auto& ptr : childs) {
        LogI("getCodecId: child symbol='{}'", ptr->info.symbol);
        if (auto box = ptr->getChild("esds"sv); box) {
            auto esds = std::dynamic_pointer_cast<BoxEsds>(box);
            return {esds->codecId, "mp4a"};
        } else if (ptr->info.symbol == "hev1" || ptr->info.symbol == "hvc1") {
            return {CodecId::HEVC, ptr->info.symbol};
        } else if (ptr->info.symbol == "avc1" || ptr->info.symbol == "avc3") {
            return {CodecId::AVC, ptr->info.symbol};
        }
    }
    LogI("getCodecId: returning Unknown");
    return {CodecId::Unknown, ""};
}

void BoxStsd::decodeAudioEntry(Buffer& buffer, std::string_view /*symbol*/) noexcept {
    buffer.skip(6);
    buffer.read2ByteBE(dataReferenceIndex);
    buffer.skip(2 + 2 + 4);
    buffer.read2ByteBE(channelCount);
    buffer.read2ByteBE(sampleSize);
    buffer.read2ByteBE(audioCompressionId);
    buffer.read2ByteBE(packetSize);
    uint32_t tSampleRate = 0;
    buffer.read4ByteBE(tSampleRate);
    sampleRate = static_cast<uint16_t>(tSampleRate >> 16);
    isAudio = true;
}

void BoxStsd::decodeVideoEntry(Buffer& buffer, std::string_view /*symbol*/) noexcept {
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

bool BoxStsd::decode(Buffer& buffer) noexcept {
    if (!buffer.require(8)) {
        return false;
    }
    buffer.readByte(version);
    buffer.read3ByteBE(flags);
    buffer.read4ByteBE(entryCount);

    for (uint32_t i = 0; i < entryCount; i++) {
        auto box = Box::createBox(buffer);
        if (!box) {
            LogE("BoxStsd::decode: createBox failed at entry {}", i);
            return false;
        }
        append(box);
        const auto endPos = box->info.start + box->info.size;
        const auto& sym = box->info.symbol;

        if (sym == "mp4a" || sym == "Opus") {
            decodeAudioEntry(buffer, sym);
        } else if (sym == "avc1" || sym == "avc3" || sym == "hvc1" || sym == "hev1") {
            decodeVideoEntry(buffer, sym);
        }

        while (buffer.pos() < endPos) {
            auto subBox = Box::createBox(buffer);
            if (!subBox) {
                break;
            }
            box->append(subBox);
            subBox->decode(buffer);
            buffer.skipTo(static_cast<int64_t>(subBox->info.end()));
            if (subBox->info.end() >= endPos) {
                break;
            }
        }
        buffer.skipTo(static_cast<int64_t>(endPos));
    }
    return true;
}

bool BoxStsc::decode(Buffer& buffer) noexcept {
    if (!buffer.require(8)) {
        return false;
    }
    buffer.skip(1 + 3); // version + flags

    uint32_t entryCount = 0;
    buffer.read4ByteBE(entryCount);

    constexpr uint32_t kMaxEntryCount = 10'000'000;
    if (entryCount > kMaxEntryCount) {
        LogE("BoxStsc: entryCount {} exceeds limit", entryCount);
        return false;
    }
    entrys.reserve(entryCount);
    for (uint32_t i = 0; i < entryCount; i++) {
        if (!buffer.require(12)) {
            return false;
        }
        StscEntry entry;
        buffer.read4ByteBE(entry.firstChunk);
        buffer.read4ByteBE(entry.samplesPerChunk);
        buffer.read4ByteBE(entry.sampleDescriptionIndex);
        entrys.push_back(entry);
    }
    return true;
}

bool BoxStsz::decode(Buffer& buffer) noexcept {
    if (!buffer.require(12)) {
        return false;
    }
    buffer.skip(1 + 3); // version + flags

    uint32_t defaultSampleSize = 0;
    buffer.read4ByteBE(defaultSampleSize);

    uint32_t entryCount = 0;
    buffer.read4ByteBE(entryCount);

    constexpr uint32_t kMaxEntryCount = 10'000'000;
    if (entryCount > kMaxEntryCount) {
        LogE("BoxStsz: entryCount {} exceeds limit", entryCount);
        return false;
    }
    if (defaultSampleSize != 0) {
        sampleSizes.assign(entryCount, defaultSampleSize);
        return true;
    }
    sampleSizes.reserve(entryCount);
    for (uint32_t i = 0; i < entryCount; i++) {
        if (!buffer.require(4)) {
            return false;
        }
        uint32_t size = 0;
        buffer.read4ByteBE(size);
        sampleSizes.push_back(size);
    }
    return true;
}

bool BoxStco::decode(Buffer& buffer) noexcept {
    if (!buffer.require(8)) {
        return false;
    }
    buffer.skip(1 + 3); // version + flags

    uint32_t entryCount = 0;
    buffer.read4ByteBE(entryCount);

    constexpr uint32_t kMaxEntryCount = 10'000'000;
    if (entryCount > kMaxEntryCount) {
        LogE("BoxStco: entryCount {} exceeds limit", entryCount);
        return false;
    }
    chunkOffsets.reserve(entryCount);
    for (uint32_t i = 0; i < entryCount; i++) {
        if (!buffer.require(4)) {
            return false;
        }
        uint32_t offset = 0;
        buffer.read4ByteBE(offset);
        chunkOffsets.push_back(offset);
    }
    return true;
}

bool BoxCo64::decode(Buffer& buffer) noexcept {
    if (!buffer.require(8)) {
        return false;
    }
    buffer.skip(1 + 3); // version + flags

    uint32_t entryCount = 0;
    buffer.read4ByteBE(entryCount);

    constexpr uint32_t kMaxEntryCount = 10'000'000;
    if (entryCount > kMaxEntryCount) {
        LogE("BoxCo64: entryCount {} exceeds limit", entryCount);
        return false;
    }
    chunkOffsets.reserve(entryCount);
    for (uint32_t i = 0; i < entryCount; i++) {
        if (!buffer.require(8)) {
            return false;
        }
        uint64_t offset = 0;
        buffer.read8ByteBE(offset);
        chunkOffsets.push_back(offset);
    }
    return true;
}

bool BoxStts::decode(Buffer& buffer) noexcept {
    if (!buffer.require(8)) {
        return false;
    }
    buffer.skip(1 + 3); // version + flags

    uint32_t entryCount = 0;
    buffer.read4ByteBE(entryCount);

    for (uint32_t i = 0; i < entryCount; i++) {
        if (!buffer.require(8)) {
            return false;
        }
        SttsEntry entry;
        buffer.read4ByteBE(entry.sampleCount);
        buffer.read4ByteBE(entry.sampleDelta);
        entrys.push_back(entry);
    }
    return true;
}

uint32_t BoxStts::sampleSize() const noexcept {
    uint32_t count = 0;
    for (auto& entry : entrys) {
        count += entry.sampleCount;
    }
    return count;
}

bool BoxCtts::decode(Buffer& buffer) noexcept {
    if (!buffer.require(8)) {
        return false;
    }
    buffer.readByte(version);
    buffer.skip(3); // flags

    uint32_t entryCount = 0;
    buffer.read4ByteBE(entryCount);

    for (size_t i = 0; i < entryCount; i++) {
        if (!buffer.require(8)) {
            return false;
        }
        CttsEntry entry;
        buffer.read4ByteBE(entry.sampleCount);
        uint32_t sampleOffset = 0;
        buffer.read4ByteBE(sampleOffset);
        entry.sampleOffset = static_cast<int32_t>(sampleOffset);
        entrys.push_back(entry);
    }
    return true;
}

bool BoxStss::decode(Buffer& buffer) noexcept {
    if (!buffer.require(8)) {
        return false;
    }
    buffer.skip(1 + 3); // version + flags
    uint32_t entryCount = 0;
    buffer.read4ByteBE(entryCount);
    for (uint32_t i = 0; i < entryCount; i++) {
        if (!buffer.require(4)) {
            return false;
        }
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
    if (!buffer.require(static_cast<uint32_t>(info.size - info.headerSize))) {
        return false;
    }
    buffer.skip(1 + 3); // version + flags

    uint8_t esDescTag = 0;
    buffer.readByte(esDescTag);
    [[maybe_unused]] auto descLen = getDescrLen(buffer);

    if (esDescTag == 0x03) {
        buffer.skip(2); // ES_ID

        uint8_t esFlags = 0;
        buffer.readByte(esFlags);
        if (esFlags & 0x01) { // streamDependenceFlag
            buffer.skip(2);
        }
        if (esFlags & 0x02) { // URL_Flag
            uint8_t len = 0;
            buffer.readByte(len);
            buffer.skip(len);
        }
        if (esFlags & 0x04) { // OCRstreamFlag
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
                decodeAudioSpecificConfig(buffer);
            }
        } else if (objectTypeId == 0x6b) {
            codecId = CodecId::MP3;
        }
    }
    return true;
}

void BoxEsds::decodeAudioSpecificConfig(Buffer& buffer) noexcept {
    BitReadView view(buffer);
    audioSpecConfig.audioObjectType = uint8_t(view.readBits(5));
    audioSpecConfig.samplingFrequencyIndex = view.readBits(4);
    if (audioSpecConfig.samplingFrequencyIndex == 0x0f) {
        audioSpecConfig.samplingFrequencyIndex = view.readBits(24);
    }
    audioSpecConfig.channelConfiguration = uint8_t(view.readBits(4));
    if (audioSpecConfig.audioObjectType == 5 || audioSpecConfig.audioObjectType == 29) {
        audioSpecConfig.samplingFreqIndexExt = view.readBits(4);
        if (audioSpecConfig.samplingFreqIndexExt == 0x0f) {
            audioSpecConfig.samplingFreqIndexExt = view.readBits(24);
        }
        audioSpecConfig.audioObjectTypeExt = uint8_t(view.readBits(5));
    }
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

std::string BoxAvcc::description(const std::string& prefix) const noexcept {
    auto desc = Box::description(prefix);
    desc.pop_back();
    return desc + std::format("version: {}, profileIdc: {}, "
                              "profileCompatibility: {}, levelIdc: {},"
                              "naluByteSize: {}\n",
                              version,
                              profileIdc,
                              profileCompatibility,
                              levelIdc,
                              naluByteSize);
}

// avcC (size)
//   |--- (size) 1byte: configurationVersion
//   |--- (size) 1byte: AVCProfileIndication
//   |--- (size) 1byte: profile_compatibility
//   |--- (size) 1byte: AVCLevelIndication
//   |--- (size) 6bit reserved + 2bit lengthSizeMinusOne
//   |--- (size) 3bit reserved + 5bit numOfSPS
//   |--- (size) 2byte: SPS length
//   |--- (size) Nbyte: SPS data
//   |--- (size) 1byte: numOfPPS
//   |--- (size) 2byte: PPS length
//   |--- (size) Nbyte: PPS data
bool BoxAvcc::decode(Buffer& buffer) noexcept {
    // version(1) + profileIdc(1) + profileCompatibility(1) + levelIdc(1) + naluByteSize(1) +
    // spsCount(1)
    if (!buffer.require(6)) {
        return false;
    }
    buffer.readByte(version);
    buffer.readByte(profileIdc);
    buffer.readByte(profileCompatibility);
    buffer.readByte(levelIdc);
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

std::string BoxHvcc::description(const std::string& prefix) const noexcept {
    return Box::description(prefix) +
           std::format("version: {}, profileSpace: {}, "
                       "tierFlag: {}, profileIdc: {},"
                       "profileCompatibility: {} , levelIdc:{}, naluByteSize:{}\n",
                       version,
                       profileSpace,
                       tierFlag,
                       profileIdc,
                       profileCompatibility,
                       levelIdc,
                       naluByteSize);
}

// hvcC (size)
//   |--- (1byte) configurationVersion:
//   |--- (1byte) general_profile_space
//   |--- (4byte) general_profile_compatibility_flags
//   |--- (6byte) general_constraint_indicator_flags
//   |--- (1byte) general_level_idc
//   |--- (2byte) min_spatial_segmentation_idc
//   |--- (1byte) parallelismType
//   |--- (1byte) chromaFormat
//   |--- (1byte) bitDepthLumaMinus8
//   |--- (1byte) bitDepthChromaMinus8
//   |--- (2byte) avgFrameRate
//   |--- (1byte) constantFrameRate, numTemporalLayers, temporalIdNested, lengthSizeMinusOne
//   |--- (1byte) numOfArrays: NALU size
//        |--- Array 1: (1byte) NAL unit type
//                     (2byte) numNalus
//                     (2byte) nalUnitLength
//                     NALU data (VPS, SPS, PPS)
//        |--- Array 2: (like Array 1)
//        |--- ...
bool BoxHvcc::decode(Buffer& buffer) noexcept {
    const auto endPos = info.size + info.start;
    buffer.readByte(version);

    uint8_t tmp = 0;
    buffer.readByte(tmp);
    profileSpace = (tmp >> 6) & 0x03;
    tierFlag = (tmp >> 5) & 0x01;
    profileIdc = tmp & 0x1f;

    buffer.read4ByteBE(profileCompatibility);
    buffer.skip(6);
    buffer.readByte(levelIdc);
    buffer.skip(2 + 1 + 1 + 1 + 1);
    buffer.read2ByteBE(avgFrameRate);

    uint8_t value = 0;
    buffer.readByte(value);
    naluByteSize = value & 0x03;

    uint8_t naluArraySize = 0;
    buffer.readByte(naluArraySize);

    for (uint8_t i = 0; i < naluArraySize; i++) {
        uint8_t naluType = 0;
        buffer.readByte(naluType);
        naluType = naluType & 0x3f;
        uint16_t naluCount = 0;
        buffer.read2ByteBE(naluCount);
        std::vector<DataRefPtr> vec;
        for (uint16_t j = 0; j < naluCount; j++) {
            uint16_t naluSize = 0;
            buffer.read2ByteBE(naluSize);
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
    if (skip > 0) {
        buffer.skip(skip);
    }
    return true;
}

bool BoxElst::decode(Buffer& buffer) noexcept {
    if (!buffer.require(8)) {
        return false;
    }
    buffer.readByte(version);
    buffer.skip(3);
    uint32_t entryCount = 0;
    buffer.read4ByteBE(entryCount);
    entries.reserve(entryCount);
    for (uint32_t i = 0; i < entryCount; ++i) {
        ElstEntry e;
        if (version == 1) {
            if (!buffer.require(20)) {
                return false;
            }
            buffer.read8ByteBE(e.segmentDuration);
            uint64_t mt = 0;
            buffer.read8ByteBE(mt);
            e.mediaTime = static_cast<int64_t>(mt);
        } else {
            if (!buffer.require(12)) {
                return false;
            }
            uint32_t sd = 0;
            buffer.read4ByteBE(sd);
            e.segmentDuration = sd;
            uint32_t mt = 0;
            buffer.read4ByteBE(mt);
            e.mediaTime = static_cast<int32_t>(mt);
        }
        buffer.read2ByteBE(reinterpret_cast<uint16_t&>(e.mediaRateInteger));
        buffer.read2ByteBE(reinterpret_cast<uint16_t&>(e.mediaRateFraction));
        entries.push_back(e);
    }
    return true;
}

int64_t BoxElst::mediaTimeOffset() const noexcept {
    for (const auto& e : entries) {
        if (e.mediaTime >= 0) {
            return e.mediaTime;
        }
    }
    return 0;
}

bool BoxElst::hasEmptyEdit() const noexcept {
    return !entries.empty() && entries.front().mediaTime == -1;
}

bool BoxTrex::decode(Buffer& buffer) noexcept {
    if (!buffer.require(24)) {
        return false;
    }
    buffer.readByte(version);
    buffer.skip(3);
    buffer.read4ByteBE(trackId);
    buffer.read4ByteBE(defaultSampleDescriptionIndex);
    buffer.read4ByteBE(defaultSampleDuration);
    buffer.read4ByteBE(defaultSampleSize);
    buffer.read4ByteBE(defaultSampleFlags);
    return true;
}

bool BoxMfhd::decode(Buffer& buffer) noexcept {
    if (!buffer.require(8)) {
        return false;
    }
    buffer.readByte(version);
    buffer.skip(3);
    buffer.read4ByteBE(sequenceNumber);
    return true;
}

bool BoxTfhd::decode(Buffer& buffer) noexcept {
    if (!buffer.require(8)) {
        return false;
    }
    buffer.readByte(version);
    buffer.read3ByteBE(flags);
    buffer.read4ByteBE(trackId);
    if (flags & kBaseDataOffsetPresent) {
        buffer.read8ByteBE(baseDataOffset);
    }
    if (flags & kSampleDescriptionIndexPresent) {
        buffer.read4ByteBE(sampleDescriptionIndex);
    }
    if (flags & kDefaultSampleDurationPresent) {
        buffer.read4ByteBE(defaultSampleDuration);
    }
    if (flags & kDefaultSampleSizePresent) {
        buffer.read4ByteBE(defaultSampleSize);
    }
    if (flags & kDefaultSampleFlagsPresent) {
        buffer.read4ByteBE(defaultSampleFlags);
    }
    return true;
}

bool BoxTfdt::decode(Buffer& buffer) noexcept {
    if (!buffer.require(4)) {
        return false;
    }
    buffer.readByte(version);
    buffer.skip(3);
    if (version == 1) {
        if (!buffer.require(8)) {
            return false;
        }
        buffer.read8ByteBE(baseMediaDecodeTime);
    } else {
        if (!buffer.require(4)) {
            return false;
        }
        uint32_t t = 0;
        buffer.read4ByteBE(t);
        baseMediaDecodeTime = t;
    }
    return true;
}

bool BoxTrun::decode(Buffer& buffer) noexcept {
    if (!buffer.require(8)) {
        return false;
    }
    buffer.readByte(version);
    buffer.read3ByteBE(flags);
    uint32_t sampleCount = 0;
    buffer.read4ByteBE(sampleCount);
    if (flags & kDataOffsetPresent) {
        uint32_t tmp = 0;
        buffer.read4ByteBE(tmp);
        dataOffset = static_cast<int32_t>(tmp);
    }
    if (flags & kFirstSampleFlagsPresent) {
        buffer.read4ByteBE(firstSampleFlags);
    }
    samples.reserve(sampleCount);
    for (uint32_t i = 0; i < sampleCount; ++i) {
        TrunSample s;
        if (flags & kSampleDurationPresent) {
            buffer.read4ByteBE(s.duration);
        }
        if (flags & kSampleSizePresent) {
            buffer.read4ByteBE(s.size);
        }
        if (flags & kSampleFlagsPresent) {
            buffer.read4ByteBE(s.flags);
        }
        if (flags & kSampleCompositionTimeOffsetPresent) {
            uint32_t tmp = 0;
            buffer.read4ByteBE(tmp);
            s.compositionTimeOffset = static_cast<int32_t>(tmp);
        }
        samples.push_back(s);
    }
    return true;
}

bool BoxSidx::decode(Buffer& buffer) noexcept {
    if (!buffer.require(12)) {
        return false;
    }
    buffer.readByte(version);
    buffer.skip(3);
    buffer.read4ByteBE(referenceId);
    buffer.read4ByteBE(timeScale);
    if (version == 0) {
        if (!buffer.require(8)) {
            return false;
        }
        uint32_t ept = 0, fo = 0;
        buffer.read4ByteBE(ept);
        buffer.read4ByteBE(fo);
        earliestPresentationTime = ept;
        firstOffset = fo;
    } else {
        if (!buffer.require(16)) {
            return false;
        }
        buffer.read8ByteBE(earliestPresentationTime);
        buffer.read8ByteBE(firstOffset);
    }
    uint16_t reserved = 0, entryCount = 0;
    buffer.read2ByteBE(reserved);
    buffer.read2ByteBE(entryCount);
    entries.reserve(entryCount);
    for (uint16_t i = 0; i < entryCount; ++i) {
        if (!buffer.require(12)) {
            return false;
        }
        SidxEntry e;
        uint32_t refField = 0, durField = 0, sapField = 0;
        buffer.read4ByteBE(refField);
        buffer.read4ByteBE(durField);
        buffer.read4ByteBE(sapField);
        e.referenceType = (refField >> 31) & 0x1;
        e.referencedSize = refField & 0x7fffffff;
        e.subsegmentDuration = durField;
        e.startsWithSap = (sapField >> 31) & 0x1;
        e.sapType = static_cast<uint8_t>((sapField >> 28) & 0x7);
        e.sapDeltaTime = sapField & 0x0fffffff;
        entries.push_back(e);
    }
    return true;
}

bool BoxTkhd::decode(Buffer& buffer) noexcept {
    if (!buffer.require(4)) {
        return false;
    }
    buffer.readByte(version);
    buffer.read3ByteBE(flags);
    if (version == 1) {
        buffer.read8ByteBE(creationTime);
        buffer.read8ByteBE(modificationTime);
        buffer.read4ByteBE(trackId);
        buffer.skip(4); // reserved
        buffer.read8ByteBE(duration);
    } else {
        uint32_t tmp = 0;
        buffer.read4ByteBE(tmp);
        creationTime = tmp;
        buffer.read4ByteBE(tmp);
        modificationTime = tmp;
        buffer.read4ByteBE(trackId);
        buffer.skip(4); // reserved
        buffer.read4ByteBE(tmp);
        duration = tmp;
    }
    buffer.skip(4 + 4 + 4 + 2 + 2); // reserved(4) + layer(2) + alternate_group(2) + volume(2) + reserved(2)
    buffer.read2ByteBE(reinterpret_cast<uint16_t&>(layer));
    buffer.read2ByteBE(reinterpret_cast<uint16_t&>(alternateGroup));
    buffer.readByte(reinterpret_cast<uint8_t&>(volume));
    buffer.skip(1 + 2); // reserved(1) + matrix 前 8 个值
    for (auto& m : matrix) {
        uint32_t v = 0;
        buffer.read4ByteBE(v);
        auto h = (v >> 16) & 0xffff;
        auto l = v & 0xffff;
        m = static_cast<float>(h) + static_cast<float>(l) / 65535.f;
    }
    // width + height (fixed-point 16.16)
    uint32_t w = 0, h = 0;
    buffer.read4ByteBE(w);
    buffer.read4ByteBE(h);
    width = w >> 16;
    height = h >> 16;
    return true;
}

bool BoxHdlr::decode(Buffer& buffer) noexcept {
    if (!buffer.require(20)) {
        return false;
    }
    buffer.readByte(version);
    buffer.read3ByteBE(flags);
    buffer.skip(4); // pre_defined
    buffer.readString(4, handlerType);
    // reserved(12) + name
    auto remaining = static_cast<uint32_t>(info.size - info.headerSize - 12);
    if (remaining > 0) {
        buffer.skip(remaining);
    }
    return true;
}

bool BoxVmhd::decode(Buffer& buffer) noexcept {
    if (!buffer.require(4)) {
        return false;
    }
    buffer.readByte(version);
    buffer.read3ByteBE(flags);
    buffer.skip(4); // graphicsmode + opcolor
    return true;
}

bool BoxSmhd::decode(Buffer& buffer) noexcept {
    if (!buffer.require(4)) {
        return false;
    }
    buffer.readByte(version);
    buffer.read3ByteBE(flags);
    buffer.skip(2); // balance
    buffer.skip(2); // reserved
    return true;
}

bool BoxDref::decode(Buffer& buffer) noexcept {
    if (!buffer.require(8)) {
        return false;
    }
    buffer.readByte(version);
    buffer.read3ByteBE(flags);
    uint32_t entryCount = 0;
    buffer.read4ByteBE(entryCount);
    for (uint32_t i = 0; i < entryCount; ++i) {
        auto box = Box::createBox(buffer);
        if (!box) {
            break;
        }
        box->decode(buffer);
        append(box);
        auto skip = static_cast<int64_t>(box->info.start + box->info.size - buffer.pos());
        if (skip > 0 && !buffer.skip(skip)) {
            break;
        }
    }
    return true;
}

} // namespace slark
