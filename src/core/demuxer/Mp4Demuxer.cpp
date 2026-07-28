//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#include "Mp4Demuxer.h"

#include <optional>
#include <ranges>
#include <stack>

#include "IDemuxer.h"
#include "MediaUtil.h"

namespace slark {

bool isContainerBox(std::string_view symbol) noexcept {
    using namespace std::string_view_literals;
    static const std::vector<std::string_view> kContainerBox = {
        "moov"sv,
        "trak"sv,
        "edts"sv,
        "mdia"sv,
        "meta"sv,
        "minf"sv,
        "stbl"sv,
        "dinf"sv,
        "udta"sv,
        "mvex"sv,
        "moof"sv,
        "traf"sv,
    };
    return std::any_of(kContainerBox.begin(), kContainerBox.end(), [symbol](const auto& view) {
        return view == symbol;
    });
}

void TrackContext::init() noexcept {
    if (elst) {
        elstMediaTimeOffset = elst->mediaTimeOffset();
    }
    if (type == TrackType::Video && ctts && !ctts->entrys.empty()) {
        dts = pts - ctts->entrys[0].sampleOffset;
        cttsEntrySampleIndex = 0;
    }
    if (elstMediaTimeOffset > 0) {
        dts -= elstMediaTimeOffset;
        pts -= elstMediaTimeOffset;
    }
}

uint64_t TrackContext::getSeekPos(double targetTime) const noexcept {
    if (!stts || !stsc || !stco || !stsz) {
        return 0;
    }

    uint32_t sampleIndex = findSampleIndexByTime(targetTime);

    uint32_t seekSampleIndex = sampleIndex;
    if (type == TrackType::Video && stss) {
        const auto& keyIndexes = stss->keyIndexs;
        auto it = std::upper_bound(keyIndexes.begin(), keyIndexes.end(), sampleIndex);
        seekSampleIndex = it != keyIndexes.begin() ? *std::prev(it) : keyIndexes.front();
    }

    auto chunkResult = findChunkBySampleIndex(seekSampleIndex);
    if (!chunkResult.has_value()) {
        return 0;
    }
    auto [chunkIndex, firstSampleInChunk] = chunkResult.value();

    if (chunkIndex >= stco->chunkOffsets.size()) {
        LogE("chunkIndex out of range");
        return 0;
    }
    const uint64_t chunkOffset = stco->chunkOffsets[chunkIndex];
    uint64_t sampleOffsetInChunk = 0;
    for (uint32_t i = 0; i < seekSampleIndex - firstSampleInChunk; ++i) {
        const uint32_t idx = firstSampleInChunk - 1 + i;
        if (idx >= stsz->sampleSizes.size()) {
            LogE("sampleSizes index out of range: {}", idx);
            break;
        }
        sampleOffsetInChunk += stsz->sampleSizes[idx];
    }
    const auto pos = chunkOffset + sampleOffsetInChunk;
    LogI("[seek info] {} keySampleIndex:{}, time:{}, pos:{}",
         type == TrackType::Video ? "video" : "audio",
         seekSampleIndex,
         targetTime,
         pos);
    return pos;
}

uint32_t TrackContext::findSampleIndexByTime(double targetTime) const noexcept {
    uint32_t sampleIndex = 0;
    const auto timeScale = static_cast<double>(mdhd->timeScale);
    const double mediaTargetTime =
        targetTime + static_cast<double>(elstMediaTimeOffset) / timeScale;
    double currentTime = 0.0;
    for (const auto& entry : stts->entrys) {
        const auto entryDuration =
            static_cast<double>(entry.sampleCount * entry.sampleDelta) / timeScale;
        if (currentTime + entryDuration > mediaTargetTime) {
            const auto offset = static_cast<uint32_t>((mediaTargetTime - currentTime) * timeScale /
                                                      entry.sampleDelta);
            sampleIndex += std::min(offset, entry.sampleCount - 1);
            break;
        }
        currentTime += entryDuration;
        sampleIndex += entry.sampleCount;
    }
    return sampleIndex == 0 ? 1 : sampleIndex;
}

std::optional<std::pair<uint32_t, uint32_t>> TrackContext::findChunkBySampleIndex(
    uint32_t seekSampleIndex) const noexcept {
    uint32_t sampleCount = 0;
    for (size_t i = 0; i < stsc->entrys.size(); ++i) {
        const auto& entry = stsc->entrys[i];
        const uint32_t nextFirstChunk = (i + 1 < stsc->entrys.size())
                                            ? stsc->entrys[i + 1].firstChunk
                                            : static_cast<uint32_t>(stco->chunkOffsets.size()) + 1;
        for (uint32_t chunk = entry.firstChunk; chunk < nextFirstChunk; ++chunk) {
            const uint32_t chunkSampleCount = entry.samplesPerChunk;
            if (seekSampleIndex <= sampleCount + chunkSampleCount) {
                return std::make_pair(chunk - 1, sampleCount + 1);
            }
            sampleCount += chunkSampleCount;
        }
    }
    LogE("not found chunkIndex");
    return std::nullopt;
}

void TrackContext::seek(uint64_t pos) noexcept {
    if (!stco || !stsz || !stsc || !stts) {
        return;
    }

    // 二分找到 pos 所在的 chunk（chunkOffsets 单调递增）
    const auto& chunks = stco->chunkOffsets;
    auto it = std::upper_bound(chunks.begin(), chunks.end(), pos);
    if (it != chunks.begin()) {
        --it;
    }
    const auto chunkIndex = static_cast<uint32_t>(std::distance(chunks.begin(), it));

    // 通过 stsc 表直接计算 chunk 前的累计 sample 数，O(stsc entries) 通常极小
    uint32_t sampleBeforeChunk = 0;
    uint32_t samplesPerChunk = 1;
    const auto& entries = stsc->entrys;
    for (size_t i = 0; i < entries.size(); ++i) {
        const uint32_t firstChunk = entries[i].firstChunk - 1;
        const uint32_t nextFirstChunk = (i + 1 < entries.size())
                                            ? entries[i + 1].firstChunk - 1
                                            : static_cast<uint32_t>(chunks.size());
        if (chunkIndex < nextFirstChunk) {
            sampleBeforeChunk += (chunkIndex - firstChunk) * entries[i].samplesPerChunk;
            samplesPerChunk = entries[i].samplesPerChunk;
            break;
        }
        sampleBeforeChunk += (nextFirstChunk - firstChunk) * entries[i].samplesPerChunk;
    }

    // 在 chunk 内线性扫描找到精确 sample（samplesPerChunk 通常很小）
    uint32_t sampleInChunk = 0;
    uint64_t sampleStart = chunks[chunkIndex];
    while (sampleInChunk < samplesPerChunk) {
        const uint32_t sampleIdx = sampleBeforeChunk + sampleInChunk;
        if (sampleIdx >= stsz->sampleSizes.size()) {
            break;
        }
        const uint64_t sampleEnd = sampleStart + stsz->sampleSizes[sampleIdx];
        if (sampleEnd > pos) {
            break;
        }
        sampleStart = sampleEnd;
        ++sampleInChunk;
    }

    uint32_t sampleIndex = sampleBeforeChunk + sampleInChunk + 1;  // 1-based
    if (stss) {
        const auto& keyIndexs = stss->keyIndexs;
        auto keyIt = std::upper_bound(keyIndexs.begin(), keyIndexs.end(), sampleIndex);
        sampleIndex = keyIt != keyIndexs.begin() ? *std::prev(keyIt) : keyIndexs.front();
    }

    // 直接通过 stts 累加计算目标 sample 的 dts，避免逐帧推进
    reset();
    seekToSample(sampleIndex);

    const auto finalPos = stco->chunkOffsets[chunkLogicIndex] + static_cast<uint64_t>(sampleOffset);
    LogI("[seek info] {} sampleIndex:{}, dts:{}, pos:{}",
         type == TrackType::Video ? "video" : "audio",
         index,
         dts,
         finalPos);
}

namespace {

// 在 RLE 表中定位第 targetIndex 个元素，返回 {entryIndex, offsetInEntry}，未找到返回 nullopt
template <typename Entry, typename CountFn>
std::optional<std::pair<uint32_t, uint32_t>> seekInRle(const std::vector<Entry>& entries,
                                                       uint32_t targetIndex,
                                                       CountFn countFn) noexcept {
    uint32_t accumulated = 0;
    for (size_t i = 0; i < entries.size(); ++i) {
        const uint32_t count = countFn(entries[i]);
        if (accumulated + count > targetIndex) {
            return std::make_pair(static_cast<uint32_t>(i), targetIndex - accumulated);
        }
        accumulated += count;
    }
    return std::nullopt;
}

}  // namespace

void TrackContext::seekToSample(uint32_t targetSampleIndex) noexcept {
    const uint32_t idx = targetSampleIndex > 0 ? targetSampleIndex - 1 : 0;

    // stsc: 定位 chunk 和 chunk 内偏移
    uint32_t stscAccumulated = 0;
    for (size_t i = 0; i < stsc->entrys.size(); ++i) {
        const auto& entry = stsc->entrys[i];
        const uint32_t firstChunk = entry.firstChunk - 1;
        const uint32_t nextFirstChunk = (i + 1 < stsc->entrys.size())
                                            ? stsc->entrys[i + 1].firstChunk - 1
                                            : static_cast<uint32_t>(stco->chunkOffsets.size());
        const uint32_t entryTotal = (nextFirstChunk - firstChunk) * entry.samplesPerChunk;
        if (stscAccumulated + entryTotal > idx) {
            const uint32_t remaining = idx - stscAccumulated;
            chunkLogicIndex = firstChunk + remaining / entry.samplesPerChunk;
            entrySampleIndex = remaining % entry.samplesPerChunk;
            stscEntryIndex = static_cast<uint32_t>(i);
            sampleOffset = 0;
            for (uint32_t s = 0; s < entrySampleIndex; ++s) {
                sampleOffset +=
                    stsz->sampleSizes[stscAccumulated +
                                      (chunkLogicIndex - firstChunk) * entry.samplesPerChunk + s];
            }
            stszSampleSizeIndex = idx;
            break;
        }
        stscAccumulated += entryTotal;
    }

    // stts: 定位 dts
    dts = 0;
    if (auto r = seekInRle(stts->entrys, idx, [](const auto& e) {
            return e.sampleCount;
        })) {
        sttsEntryIndex = r->first;
        sttsEntrySampleIndex = r->second;
        for (size_t i = 0; i < sttsEntryIndex; ++i) {
            dts += static_cast<int64_t>(stts->entrys[i].sampleCount) * stts->entrys[i].sampleDelta;
        }
        dts +=
            static_cast<int64_t>(sttsEntrySampleIndex) * stts->entrys[sttsEntryIndex].sampleDelta;
    }

    // ctts: 定位 pts
    if (!ctts) {
        pts = dts;
    } else if (auto r = seekInRle(ctts->entrys, idx, [](const auto& e) {
                   return e.sampleCount;
               })) {
        cttsEntryIndex = r->first;
        cttsEntrySampleIndex = r->second;
        pts = dts + ctts->entrys[cttsEntryIndex].sampleOffset;
    }

    index = targetSampleIndex;
    LogI("[debug] seekToSample {} done: idx={}, dts={}({:.3f}s), pts={}({:.3f}s), cttsIdx={}, "
         "cttsSample={}, cttsOffset={}",
         type == TrackType::Video ? "video" : "audio",
         idx,
         dts,
         double(dts) / mdhd->timeScale,
         pts,
         double(pts) / mdhd->timeScale,
         cttsEntryIndex,
         cttsEntrySampleIndex,
         ctts ? ctts->entrys[cttsEntryIndex].sampleOffset : 0);
}

void TrackContext::reset() noexcept {
    stszSampleSizeIndex = 0;
    stscEntryIndex = 0;
    entrySampleIndex = 0;
    chunkLogicIndex = 0;
    sampleOffset = 0;

    sttsEntryIndex = 0;
    sttsEntrySampleIndex = 0;

    cttsEntryIndex = 0;
    cttsEntrySampleIndex = 0;
    keyIndex = 0;

    dts = 0;
    pts = 0;
    index = 0;
    isCompleted = false;
    fragSampleIndex = 0;
    fragmentDurationTicks = 0;
}

void TrackContext::advanceStscState() noexcept {
    auto size = stsz->sampleSizes[stszSampleSizeIndex];
    auto& entry = stsc->entrys[stscEntryIndex];
    stszSampleSizeIndex++;
    sampleOffset += size;
    entrySampleIndex++;
    if (entrySampleIndex >= entry.samplesPerChunk) {
        if (chunkLogicIndex + 1 < stco->chunkOffsets.size()) {
            chunkLogicIndex++;
        } else {
            isCompleted = true;
        }
        sampleOffset = 0;
        entrySampleIndex = 0;
    }
    if (stscEntryIndex + 1 < stsc->entrys.size()) {
        if (chunkLogicIndex >= (stsc->entrys[stscEntryIndex + 1].firstChunk - 1)) {
            stscEntryIndex++;
        }
    }
}

void TrackContext::advanceSttsState() noexcept {
    const auto delta = stts->entrys[sttsEntryIndex].sampleDelta;
    sttsEntrySampleIndex++;
    if (sttsEntrySampleIndex >= stts->entrys[sttsEntryIndex].sampleCount) {
        sttsEntrySampleIndex = 0;
        if (sttsEntryIndex + 1 < stts->entrys.size()) {
            sttsEntryIndex++;
        }
    }
    dts += delta;
}

void TrackContext::advanceCttsState() noexcept {
    if (!ctts) {
        pts = dts;
        return;
    }
    cttsEntrySampleIndex++;
    if (cttsEntrySampleIndex >= ctts->entrys[cttsEntryIndex].sampleCount) {
        if (cttsEntryIndex + 1 < ctts->entrys.size()) {
            cttsEntryIndex++;
        }
        cttsEntrySampleIndex = 0;
    }
    pts = dts + ctts->entrys[cttsEntryIndex].sampleOffset;
}

void TrackContext::calcIndex() noexcept {
    advanceStscState();
    advanceSttsState();
    advanceCttsState();
}

bool TrackContext::isInRange(Buffer& buffer, uint64_t* outOffset) const noexcept {
    if (isFragmented) {
        if (fragSampleIndex >= fragSamples.size()) {
            return false;
        }
        const auto& s = fragSamples[fragSampleIndex];
        const auto bufferStart = buffer.offset();
        const auto bufferEnd = bufferStart + buffer.totalLength();
        if (bufferStart <= s.offset && s.offset + s.size <= bufferEnd) {
            if (outOffset) {
                *outOffset = s.offset;
            }
            return true;
        }
        return false;
    }
    if (stszSampleSizeIndex >= stsz->sampleSizes.size() || stscEntryIndex >= stsc->entrys.size()) {
        return false;
    }
    auto start = stco->chunkOffsets[chunkLogicIndex] + static_cast<uint64_t>(sampleOffset);
    auto end = start + stsz->sampleSizes[stszSampleSizeIndex];
    auto bufferStart = buffer.offset();
    auto bufferEnd = bufferStart + buffer.totalLength();
    if (bufferStart <= start && end <= bufferEnd) {
        if (outOffset) {
            *outOffset = start;
        }
        return true;
    }
    return false;
}

AVFramePtrArray TrackContext::parseNaluFrames(AVFramePtr frame,
                                              DataPtr data,
                                              std::shared_ptr<VideoFrameInfo> frameInfo,
                                              const NaluClassifier& classifier) {
    AVFramePtrArray frames;
    uint32_t duration = frame->duration;
    if (duration == 0 && stts && sttsEntryIndex < stts->entrys.size()) {
        const auto& sttsEntry = stts->entrys[sttsEntryIndex];
        duration = static_cast<uint32_t>(static_cast<double>(sttsEntry.sampleDelta) /
                                         static_cast<double>(frame->timeScale) * 1000.0);
    }

    const uint32_t headerSize = naluByteSize + 1;
    const auto rawView = data->view();
    uint32_t pos = 0;

    while (pos + headerSize <= rawView.length()) {
        uint32_t naluSize = 0;
        auto sizeView = rawView.substr(pos, 4);
        sizeView.readNumberBE(naluSize, headerSize);
        const uint32_t totalSize = naluSize + headerSize;
        if (pos + totalSize > rawView.length()) {
            break;  // 防止越界
        }

        uint8_t naluHeader = 0;
        auto naluHeaderView = rawView.substr(pos + 4, 1);
        naluHeaderView.readNumberLE(naluHeader, 1);

        const auto [shouldProcess, frameType] = classifier(naluHeader);
        if (!shouldProcess) {
            pos += totalSize;
            continue;
        }

        auto info = std::make_shared<VideoFrameInfo>();
        frameInfo->copy(info);
        info->frameType = frameType;
        info->isIDRFrame = (frameType == VideoFrameType::IFrame);
        if (info->isIDRFrame) {
            keyIndex = frame->index;
        }
        info->keyIndex = keyIndex;

        pos += totalSize;
        const bool isLast = (pos >= rawView.length());

        // 最后一个 NALU 直接 move frame，其余 copy；data 统一切片
        auto newFrame = isLast ? std::move(frame) : frame->copy();
        newFrame->duration = duration;
        newFrame->info = std::move(info);
        newFrame->data = std::make_unique<Data>(rawView.substr(pos - totalSize, totalSize));
        frames.push_back(std::move(newFrame));
    }
    return frames;
}

AVFramePtrArray TrackContext::parseH264FrameData(AVFramePtr frame,
                                                 DataPtr data,
                                                 std::shared_ptr<VideoFrameInfo> frameInfo) {
    if (naluByteSize == 0) {
        auto entryBox = stsd->getChild("avc1");
        if (!entryBox) {
            entryBox = stsd->getChild("avc3");
        }
        if (entryBox) {
            auto avccBox = std::dynamic_pointer_cast<BoxAvcc>(entryBox->getChild("avcC"));
            naluByteSize = avccBox ? avccBox->naluByteSize : 3;
        } else {
            naluByteSize = 3;
        }
    }
    return parseNaluFrames(std::move(frame),
                           std::move(data),
                           std::move(frameInfo),
                           [](uint8_t naluHeader) -> std::pair<bool, VideoFrameType> {
                               const uint8_t naluType = naluHeader & 0x1f;
                               if (naluType != 5 && naluType != 1) {
                                   return {false, VideoFrameType::IFrame};
                               }
                               if (naluType == 5) {
                                   return {true, VideoFrameType::IFrame};
                               }
                               // naluType == 1: parse slice type
                               // Note: slice type parsing happens after data extraction;
                               // return PFrame as default, caller may override via
                               // parseAvcSliceType
                               return {true, VideoFrameType::PFrame};
                           });
}

AVFramePtrArray TrackContext::parseH265FrameData(AVFramePtr frame,
                                                 DataPtr data,
                                                 std::shared_ptr<VideoFrameInfo> frameInfo) {
    if (naluByteSize == 0) {
        auto entryBox = stsd->getChild("hvc1");
        if (!entryBox) {
            entryBox = stsd->getChild("hev1");
        }
        if (entryBox) {
            auto hvccBox = std::dynamic_pointer_cast<BoxHvcc>(entryBox->getChild("hvcC"));
            naluByteSize = hvccBox ? hvccBox->naluByteSize : 3;
        } else {
            naluByteSize = 3;
        }
    }
    // HEVC: IDR_W_RADL(19), IDR_N_LP(20), TRAIL_R(1), TRAIL_N(0), valid range [0, 21]
    return parseNaluFrames(std::move(frame),
                           std::move(data),
                           std::move(frameInfo),
                           [](uint8_t naluHeader) -> std::pair<bool, VideoFrameType> {
                               const uint8_t naluType = (naluHeader >> 1) & 0x3f;
                               if (naluType > 21) {
                                   return {false, VideoFrameType::IFrame};
                               }
                               if (naluType >= 16 && naluType <= 21) {
                                   return {true, VideoFrameType::IFrame};
                               }
                               return {true, VideoFrameType::PFrame};
                           });
}

void TrackContext::parseFragmentedData(Buffer& buffer,
                                       std::shared_ptr<FrameInfo> frameInfo,
                                       AVFramePtrArray& packets) {
    if (fragSampleIndex >= fragSamples.size()) {
        isCompleted = true;
        return;
    }
    const auto& s = fragSamples[fragSampleIndex];
    if (!buffer.skipTo(static_cast<int64_t>(s.offset))) {
        return;
    }
    auto frame = std::make_unique<AVFrame>();
    frame->index = index++;
    frame->dts = s.dts;
    frame->pts = s.pts;
    frame->offset = s.offset;
    frame->timeScale = mdhd->timeScale;
    if (type == TrackType::Audio) {
        frame->frameType = AVFrameType::Audio;
        auto info = std::dynamic_pointer_cast<AudioFrameInfo>(frameInfo);
        frame->duration = static_cast<uint32_t>(info->duration(s.size) * 1000.0);
        frame->info = frameInfo;
        frame->data = buffer.readData(s.size);
        packets.push_back(std::move(frame));
    } else if (type == TrackType::Video) {
        frame->frameType = AVFrameType::Video;
        if (s.duration > 0 && mdhd->timeScale > 0) {
            frame->duration = static_cast<uint32_t>(static_cast<double>(s.duration) /
                                                    static_cast<double>(mdhd->timeScale) * 1000.0);
        }
        auto data = buffer.readData(s.size);
        auto videoInfo = std::dynamic_pointer_cast<VideoFrameInfo>(frameInfo);
        AVFramePtrArray frames;
        if (codecId == CodecId::AVC) {
            frames = parseH264FrameData(std::move(frame), std::move(data), videoInfo);
        } else if (codecId == CodecId::HEVC) {
            frames = parseH265FrameData(std::move(frame), std::move(data), videoInfo);
        }
        packets.insert(packets.end(),
                       std::make_move_iterator(frames.begin()),
                       std::make_move_iterator(frames.end()));
    }
    ++fragSampleIndex;
    if (fragSampleIndex >= fragSamples.size()) {
        isCompleted = true;
    }
}

void TrackContext::parseNormalData(Buffer& buffer,
                                   uint64_t start,
                                   std::shared_ptr<FrameInfo> frameInfo,
                                   AVFramePtrArray& packets) {
    const auto size = stsz->sampleSizes[stszSampleSizeIndex];
    if (!buffer.skipTo(static_cast<int64_t>(start))) {
        return;
    }
    auto frame = std::make_unique<AVFrame>();
    frame->index = index++;
    frame->dts = dts;
    frame->pts = pts;
    frame->offset = start;
    frame->timeScale = mdhd->timeScale;
    if (type == TrackType::Audio) {
        frame->frameType = AVFrameType::Audio;
        auto info = std::dynamic_pointer_cast<AudioFrameInfo>(frameInfo);
        frame->duration = static_cast<uint32_t>(info->duration(size) * 1000.0);
        frame->info = frameInfo;
        frame->data = buffer.readData(size);
        packets.push_back(std::move(frame));
    } else if (type == TrackType::Video) {
        frame->frameType = AVFrameType::Video;
        auto data = buffer.readData(size);
        auto videoInfo = std::dynamic_pointer_cast<VideoFrameInfo>(frameInfo);
        AVFramePtrArray frames;
        if (codecId == CodecId::AVC) {
            frames = parseH264FrameData(std::move(frame), std::move(data), videoInfo);
        } else if (codecId == CodecId::HEVC) {
            frames = parseH265FrameData(std::move(frame), std::move(data), videoInfo);
        }
        packets.insert(packets.end(),
                       std::make_move_iterator(frames.begin()),
                       std::make_move_iterator(frames.end()));
    }
    calcIndex();
}

void TrackContext::parseData(Buffer& buffer,
                             std::shared_ptr<FrameInfo> frameInfo,
                             AVFramePtrArray& packets) {
    uint64_t start = 0;
    if (!isInRange(buffer, &start)) {
        return;
    }
    if (isFragmented) {
        parseFragmentedData(buffer, std::move(frameInfo), packets);
    } else {
        parseNormalData(buffer, start, std::move(frameInfo), packets);
    }
}

bool Mp4Demuxer::probeMoovBox(Buffer& buffer, int64_t& start, uint32_t& size) noexcept {
    auto view = buffer.view();
    auto pos = view.find("moov");
    if (pos == std::string_view::npos) {
        return false;
    }
    auto boxStart = pos - 4;
    start = static_cast<int64_t>(boxStart + buffer.offset());
    if (start < 0) {
        return false;
    }
    auto sizeView = view.substr(boxStart, 4);
    return sizeView.readNumberBE(size, 4);
}

void Mp4Demuxer::handleBoxInMoov(const BoxRefPtr& box,
                                 std::shared_ptr<BoxMdhd>& mdhdBox,
                                 std::stack<BoxRefPtr>& parentBox) noexcept {
    if (box->info.symbol == "stsd") {
        auto stsdBox = std::dynamic_pointer_cast<BoxStsd>(box);
        SAssert(stsdBox != nullptr, "parse error");
        LogI("stsd: children={}, isAudio={}, isVideo={}",
             stsdBox->childs.size(),
             stsdBox->isAudio,
             stsdBox->isVideo);
        auto [codecId, mediaBoxName] = stsdBox->getCodecId();
        LogI("stsd: codecId={}, mediaBoxName={}", static_cast<int32_t>(codecId), mediaBoxName);
        if (codecId != CodecId::Unknown) {
            auto track = std::make_shared<TrackContext>();
            track->stbl = parentBox.top();
            track->mdhd = mdhdBox;
            track->type = stsdBox->isAudio ? TrackType::Audio : TrackType::Video;
            track->codecId = codecId;
            track->mediaBoxName = mediaBoxName;
            tracks_[codecId] = std::move(track);
        } else {
            LogI("Unknown codec!");
        }
    } else if (box->info.symbol == "mdhd") {
        mdhdBox = std::dynamic_pointer_cast<BoxMdhd>(box);
    } else if (box->info.symbol == "trex") {
        auto trexBox = std::dynamic_pointer_cast<BoxTrex>(box);
        if (trexBox) {
            isFragmented_ = true;
            for (auto& [codecId, track] : tracks_) {
                if (track->trackId == trexBox->trackId) {
                    track->trex = trexBox;
                    track->isFragmented = true;
                    break;
                }
            }
        }
    }
}

bool Mp4Demuxer::parseMoovBox(Buffer& buffer, const BoxRefPtr& moovBox) noexcept {
    std::stack<BoxRefPtr> parentBox;
    parentBox.push(moovBox);
    auto size = moovBox->info.size - moovBox->info.headerSize;
    if (!buffer.require(size)) {
        LogE("parseMoovBox: buffer require {} bytes failed", size);
        return false;
    }
    bool res = true;
    std::shared_ptr<BoxMdhd> mdhdBox;
    uint32_t currentTrackId = 0;
    std::unordered_map<uint32_t, std::shared_ptr<BoxElst>> pendingElst;  // trackId -> elst
    while ((buffer.pos() - moovBox->info.start) < moovBox->info.size) {
        auto box = Box::createBox(buffer);
        if (!box) {
            LogE("parseMoovBox: createBox failed at pos:{}", buffer.pos());
            res = false;
            break;
        }
        box->decode(buffer);

        LogD("parseMoovBox: box={}, start={}, size={}, trackCnt={}",
             box->info.symbol,
             box->info.start,
             box->info.size,
             tracks_.size());

        if (box->info.symbol == "tkhd") {
            const auto dataStart = static_cast<int64_t>(box->info.start + box->info.headerSize);
            const auto savedPos = static_cast<int64_t>(buffer.pos());
            buffer.skipTo(dataStart);
            uint8_t tkhdVersion = 0;
            buffer.readByte(tkhdVersion);
            buffer.skip(3);
            if (tkhdVersion == 1) {
                buffer.skip(8 + 8);
            } else {
                buffer.skip(4 + 4);
            }
            buffer.read4ByteBE(currentTrackId);
            // 跳到 matrix 字段: reserved(4) + duration(4/8) + reserved(8) + layer(2) +
            // alternate_group(2) + volume(2) + reserved(2)
            if (tkhdVersion == 1) {
                buffer.skip(4 + 8 + 8 + 2 + 2 + 2 + 2);
            } else {
                buffer.skip(4 + 4 + 8 + 2 + 2 + 2 + 2);
            }
            auto readFixed16_16 = [&]() -> int32_t {
                uint32_t value = 0;
                buffer.read4ByteBE(value);
                return static_cast<int32_t>(value);
            };
            int32_t matrix[9];
            for (auto& m : matrix) {
                m = readFixed16_16();
            }
            // matrix[0..3] = a,b,c,d (单位矩阵 = 1<<16)
            constexpr int32_t kUnity = 1 << 16;
            int32_t rotation = 0;
            if (matrix[0] == 0 && matrix[1] == kUnity && matrix[3] == -kUnity && matrix[4] == 0) {
                rotation = 90;
            } else if (matrix[0] == -kUnity && matrix[1] == 0 && matrix[3] == 0 &&
                       matrix[4] == -kUnity) {
                rotation = 180;
            } else if (matrix[0] == 0 && matrix[1] == -kUnity && matrix[3] == kUnity &&
                       matrix[4] == 0) {
                rotation = 270;
            }
            trackRotations_[currentTrackId] = rotation;
            buffer.skipTo(savedPos);
        }

        handleBoxInMoov(box, mdhdBox, parentBox);

        if (box->info.symbol == "elst") {
            // elst 在 mdhd/stsd 之前，暂存等 track 创建后关联
            if (currentTrackId > 0) {
                pendingElst[currentTrackId] = std::dynamic_pointer_cast<BoxElst>(box);
            }
        }

        if (box->info.symbol == "stsd") {
            for (auto& [codecId, track] : tracks_) {
                if (track->trackId == 0) {
                    track->trackId = currentTrackId;
                    if (auto it = pendingElst.find(currentTrackId); it != pendingElst.end()) {
                        track->elst = it->second;
                    }
                }
            }
        }

        parentBox.top()->append(box);
        // udta 在 moov 顶层时跳过深度遍历，避免其异常大的 size 吞掉后续 trak
        // udta/meta 在 moov 顶层时跳过深度遍历，避免异常大的 size 吞掉后续 trak
        if ((box->info.symbol == "udta" || box->info.symbol == "meta") && parentBox.size() == 1) {
            LogD("parseMoovBox: skip '{}' as leaf at moov top level, size={}",
                 box->info.symbol,
                 box->info.size);
            auto skipOffset = static_cast<int64_t>(box->info.start + box->info.size - buffer.pos());
            if (!buffer.skip(skipOffset)) {
                LogW("parseMoovBox: skip '{}' failed at pos:{}, size:{}",
                     box->info.symbol,
                     buffer.pos(),
                     box->info.size);
                if (tracks_.empty()) {
                    res = false;
                }
                break;
            }
            continue;
        }
        if (isContainerBox(box->info.symbol)) {
            // 容器边界校验：子容器超出父容器范围时降级为 leaf skip
            if (box->info.end() > parentBox.top()->info.end()) {
                LogW("parseMoovBox: container '{}' [{},{}) exceeds parent '{}' [{},{}), skip as "
                     "leaf",
                     box->info.symbol,
                     box->info.start,
                     box->info.end(),
                     parentBox.top()->info.symbol,
                     parentBox.top()->info.start,
                     parentBox.top()->info.end());
                auto skipOffset =
                    static_cast<int64_t>(box->info.start + box->info.size - buffer.pos());
                if (!buffer.skip(skipOffset)) {
                    break;
                }
                continue;
            }
            parentBox.push(box);
            continue;
        }
        auto skipEnd = box->info.start + box->info.size;
        // 钳制 leaf box skip 不超出父容器边界
        auto parentEnd = parentBox.top()->info.start + parentBox.top()->info.size;
        if (skipEnd > parentEnd) {
            LogW("parseMoovBox: leaf '{}' skip [{},{}) clamped to parent '{}' end {}",
                 box->info.symbol,
                 box->info.start,
                 skipEnd,
                 parentBox.top()->info.symbol,
                 parentEnd);
            skipEnd = parentEnd;
        }
        auto skipOffset = static_cast<int64_t>(skipEnd - buffer.pos());
        if (!buffer.skip(skipOffset)) {
            LogW("parseMoovBox: skip {} bytes failed at pos:{}, box:{}, parentDepth:{}",
                 skipOffset,
                 buffer.pos(),
                 box->info.symbol,
                 parentBox.size());
            // 恢复：向上弹出父容器，跳到最近的合法容器边界继续解析
            bool recovered = false;
            while (parentBox.size() > 1) {
                auto container = parentBox.top();
                auto containerEnd =
                    static_cast<int64_t>(container->info.start + container->info.size);
                if (buffer.skipTo(containerEnd)) {
                    LogW("parseMoovBox: recovered to container '{}' end at pos:{}",
                         container->info.symbol,
                         containerEnd);
                    parentBox.pop();
                    recovered = true;
                    break;
                }
                parentBox.pop();
            }
            if (!recovered) {
                if (tracks_.empty()) {
                    LogE("parseMoovBox: recovery failed, no track created, moov parse failed");
                    res = false;
                }
                break;
            }
            continue;
        }
        const auto endPos = box->info.size + box->info.start;
        while (!parentBox.empty()) {
            if (endPos >= (parentBox.top()->info.start + parentBox.top()->info.size)) {
                parentBox.pop();
            } else {
                break;
            }
        }
    }
    return res;
}

bool Mp4Demuxer::open(std::unique_ptr<Buffer>& buffer) noexcept {
    if (!buffer) {
        LogE("open: buffer is null");
        return false;
    }

    if (rootBox_ == nullptr) {
        BoxInfo info;
        info.start = 0;
        info.size = 0;
        rootBox_ = std::make_shared<Box>(std::move(info));
    }
    auto res = false;
    auto eof = false;
    while (!buffer->empty()) {
        auto box = Box::createBox(*buffer);
        if (!box) {
            eof = true;
            break;
        }

        if (box->info.symbol == "moov") {
            if (!buffer->require(box->info.size - box->info.headerSize)) {
                buffer->skipTo(static_cast<int64_t>(box->info.start));
                eof = true;
                break;
            }
            res = parseMoovBox(*buffer, box);
            if (!res) {
                LogE("open: parseMoovBox failed");
                break;
            }
            rootBox_->append(box);
        } else if (box->info.symbol == "mdat") {
            headerInfo_ = std::make_unique<DemuxerHeaderInfo>();
            headerInfo_->headerLength = box->info.start;
            headerInfo_->dataSize = box->info.size;
            rootBox_->append(box);
            if (!res) {
                int64_t moovStart = 0;
                uint32_t moovSize = 0;
                if (probeMoovBox(*buffer, moovStart, moovSize)) {
                    auto currentFilePos = static_cast<int64_t>(buffer->offset() + buffer->pos());
                    auto skipBytes = moovStart - currentFilePos;
                    if (!buffer->skip(skipBytes)) {
                        break;
                    }
                    continue;
                }
                uint64_t mdatEnd = box->info.start + box->info.size;
                pendingSeekPos_ = mdatEnd;
                eof = true;
                break;
            }
        } else if (box->info.symbol == "sidx") {
            auto sidxBox = std::dynamic_pointer_cast<BoxSidx>(box);
            if (sidxBox) {
                sidxBox->decode(*buffer);
                uint64_t segOffset = box->info.end() + sidxBox->firstOffset;
                uint64_t segTime = sidxBox->earliestPresentationTime;
                uint64_t totalSegDuration = 0;
                for (auto& [codecId, track] : tracks_) {
                    track->sidxSegments.clear();
                }
                for (const auto& entry : sidxBox->entries) {
                    if (!entry.referenceType) {
                        for (auto& [codecId, track] : tracks_) {
                            TrackContext::SidxSegment seg;
                            seg.offset = segOffset;
                            seg.duration = entry.subsegmentDuration;
                            seg.startTime = segTime;
                            track->sidxSegments.push_back(seg);
                        }
                        segOffset += entry.referencedSize;
                        segTime += entry.subsegmentDuration;
                        totalSegDuration += entry.subsegmentDuration;
                    }
                }
                // fMP4 的 mvhd.duration 可能为 0，从 sidx 总 duration 补算
                if (totalDuration_.second() <= 0.0 && !tracks_.empty()) {
                    auto& firstTrack = tracks_.begin()->second;
                    if (firstTrack->mdhd) {
                        double dur = static_cast<double>(totalSegDuration) /
                                     static_cast<double>(firstTrack->mdhd->timeScale);
                        if (dur > 0.0) {
                            totalDuration_ = CTime(dur);
                            LogI("fMP4 duration computed from sidx: {:.3f}s", dur);
                        }
                    }
                }
            }
            rootBox_->append(box);
        } else if (box->info.symbol == "moof") {
            parseMoofChildren(*buffer, box);
            parseMoofBox(*buffer, box);
            rootBox_->append(box);
        } else {
            rootBox_->append(box);
        }

        const auto endPos = box->info.size + box->info.start;
        const auto skipOffset = endPos - buffer->pos();
        if (!buffer->skip(static_cast<int64_t>(skipOffset))) {
            if (box->info.symbol != "mdat") {
                res = false;
            }
            break;
        }
    }

    if (!res) {
        if (eof) {
            return false;
        }
        LogI("open: need more data, mdat at [{}, {})",
             headerInfo_ ? headerInfo_->headerLength : 0,
             headerInfo_ ? headerInfo_->headerLength + headerInfo_->dataSize : 0);
    } else {
        LogI("open: moov parsed");
        initData();
    }
    return res;
}

void initTrack(std::shared_ptr<TrackContext>& track) {
    using BoxSetter = std::function<void(const BoxRefPtr&, TrackContext&)>;
    static const std::unordered_map<std::string_view, BoxSetter> kBoxSetters = {
        {"stsd",
         [](const BoxRefPtr& b, TrackContext& t) {
             t.stsd = std::dynamic_pointer_cast<BoxStsd>(b);
         }},
        {"stts",
         [](const BoxRefPtr& b, TrackContext& t) {
             t.stts = std::dynamic_pointer_cast<BoxStts>(b);
         }},
        {"stsz",
         [](const BoxRefPtr& b, TrackContext& t) {
             t.stsz = std::dynamic_pointer_cast<BoxStsz>(b);
         }},
        {"stco",
         [](const BoxRefPtr& b, TrackContext& t) {
             t.stco = std::dynamic_pointer_cast<BoxStco>(b);
         }},
        {"co64",
         [](const BoxRefPtr& b, TrackContext& t) {
             auto co64 = std::dynamic_pointer_cast<BoxCo64>(b);
             if (co64) {
                 auto stco = std::make_shared<BoxStco>(BoxInfo{});
                 stco->chunkOffsets = co64->chunkOffsets;
                 t.stco = std::move(stco);
             }
         }},
        {"stsc",
         [](const BoxRefPtr& b, TrackContext& t) {
             t.stsc = std::dynamic_pointer_cast<BoxStsc>(b);
         }},
        {"ctts",
         [](const BoxRefPtr& b, TrackContext& t) {
             t.ctts = std::dynamic_pointer_cast<BoxCtts>(b);
         }},
        {"stss",
         [](const BoxRefPtr& b, TrackContext& t) {
             t.stss = std::dynamic_pointer_cast<BoxStss>(b);
         }},
    };
    if (track->stbl) {
        for (auto& child : track->stbl->childs) {
            if (auto it = kBoxSetters.find(child->info.symbol); it != kBoxSetters.end()) {
                it->second(child, *track);
            }
        }
    }
    track->init();
}

void Mp4Demuxer::initAudioTrack(const std::shared_ptr<TrackContext>& track) noexcept {
    auto stsdBox = track->stsd;
    audioInfo_ = std::make_shared<AudioInfo>();
    audioInfo_->channels = stsdBox->channelCount;
    audioInfo_->sampleRate = stsdBox->sampleRate;
    audioInfo_->bitsPerSample = stsdBox->sampleSize;
    audioInfo_->timeScale = track->mdhd->timeScale;
    audioInfo_->mediaInfo =
        track->codecId == CodecId::AAC ? MEDIA_MIMETYPE_AUDIO_AAC : MEDIA_MIMETYPE_UNKNOWN;

    if (track->codecId == CodecId::AAC) {
        auto esdsBox = std::dynamic_pointer_cast<BoxEsds>(
            stsdBox->getChild(track->mediaBoxName)->getChild("esds"));
        if (esdsBox) {
            audioInfo_->channels = esdsBox->audioSpecConfig.channelConfiguration;
            audioInfo_->profile = getAudioProfile(esdsBox->audioSpecConfig.audioObjectType);
            audioInfo_->samplingFrequencyIndex = esdsBox->audioSpecConfig.samplingFrequencyIndex;
            audioInfo_->audioObjectTypeExt = esdsBox->audioSpecConfig.audioObjectTypeExt;
            audioInfo_->samplingFreqIndexExt = esdsBox->audioSpecConfig.samplingFreqIndexExt;
        } else {
            LogW("initAudioTrack: esds box not found, use stsd default");
        }
    }
    LogI("initAudioTrack: channels:{}, sampleRate:{}, bitsPerSample:{}, timeScale:{}",
         audioInfo_->channels,
         audioInfo_->sampleRate,
         audioInfo_->bitsPerSample,
         audioInfo_->timeScale);
}

void Mp4Demuxer::initVideoTrack(const std::shared_ptr<TrackContext>& track) noexcept {
    auto stsdBox = track->stsd;
    videoInfo_ = std::make_shared<VideoInfo>();
    videoInfo_->width = stsdBox->width;
    videoInfo_->height = stsdBox->height;
    videoInfo_->timeScale = track->mdhd->timeScale;
    if (auto it = trackRotations_.find(track->trackId); it != trackRotations_.end()) {
        videoInfo_->rotationDegrees = it->second;
    }

    auto sttsBox = track->stts;
    if (sttsBox && !sttsBox->entrys.empty()) {
        const auto delta = sttsBox->entrys.front().sampleDelta;
        if (delta > 0) {
            videoInfo_->fps = static_cast<uint16_t>(videoInfo_->timeScale / delta);
        } else {
            const auto sampleSize = static_cast<double>(sttsBox->sampleSize());
            videoInfo_->fps = static_cast<uint16_t>(ceil(sampleSize / totalDuration_.second()));
        }
    } else if (!track->fragSamples.empty()) {
        const auto& s = track->fragSamples.front();
        if (s.duration > 0) {
            videoInfo_->fps = static_cast<uint16_t>(videoInfo_->timeScale / s.duration);
        }
    }

    if (track->codecId == CodecId::AVC) {
        videoInfo_->mediaInfo = MEDIA_MIMETYPE_VIDEO_AVC;
        auto avccBox = std::dynamic_pointer_cast<BoxAvcc>(
            stsdBox->getChild(track->mediaBoxName)->getChild("avcC"));
        if (avccBox) {
            if (!avccBox->sps.empty()) {
                videoInfo_->sps = avccBox->sps.front();
            }
            if (!avccBox->pps.empty()) {
                videoInfo_->pps = avccBox->pps.front();
            }
            videoInfo_->naluHeaderLength = avccBox->naluByteSize + 1;
            videoInfo_->profile = avccBox->profileIdc;
            videoInfo_->level = avccBox->levelIdc;
        } else {
            LogW("initVideoTrack: avcC box not found");
        }
    } else if (track->codecId == CodecId::HEVC) {
        videoInfo_->mediaInfo = MEDIA_MIMETYPE_VIDEO_HEVC;
        auto hvccBox = std::dynamic_pointer_cast<BoxHvcc>(
            stsdBox->getChild(track->mediaBoxName)->getChild("hvcC"));
        if (hvccBox) {
            if (!hvccBox->sps.empty()) {
                videoInfo_->sps = hvccBox->sps.front();
            }
            if (!hvccBox->pps.empty()) {
                videoInfo_->pps = hvccBox->pps.front();
            }
            if (!hvccBox->vps.empty()) {
                videoInfo_->vps = hvccBox->vps.front();
            }
            videoInfo_->naluHeaderLength = hvccBox->naluByteSize + 1;
            videoInfo_->profile = hvccBox->profileIdc;
            videoInfo_->level = hvccBox->levelIdc;
        } else {
            LogW("initVideoTrack: hvcC box not found");
        }
    } else {
        videoInfo_->mediaInfo = MEDIA_MIMETYPE_UNKNOWN;
    }
    LogI("initVideoTrack: {}x{}, fps:{}, timeScale:{}",
         videoInfo_->width,
         videoInfo_->height,
         videoInfo_->fps,
         videoInfo_->timeScale);
}

void Mp4Demuxer::initData() noexcept {
    auto mvhdBox = std::dynamic_pointer_cast<BoxMvhd>(rootBox_->getChild("moov")->getChild("mvhd"));
    totalDuration_ =
        CTime(static_cast<double>(mvhdBox->duration) / static_cast<double>(mvhdBox->timeScale));
    // fMP4 的 mvhd.duration 可能为 0，后面 parse 到 sidx 后会补算
    for (auto& [codecId, track] : tracks_) {
        initTrack(track);
        if (!track->stsd) {
            LogI("get stsd error");
            continue;
        }
        if (track->type == TrackType::Audio) {
            initAudioTrack(track);
        } else if (track->type == TrackType::Video) {
            initVideoTrack(track);
        }
    }
    isOpened_ = true;
    buffer_ = std::make_unique<Buffer>(rootBox_->info.size);
    LogI("initData: duration:{:.3f}s, tracks:{}, fragmented:{}",
         totalDuration_.second(),
         static_cast<int32_t>(tracks_.size()),
         isFragmented_);
}

CTime Mp4Demuxer::computeTotalDuration() const noexcept {
    if (totalDuration_.second() > 0.0) {
        return totalDuration_;
    }
    if (!isFragmented_) {
        return totalDuration_;
    }
    // fMP4: 优先从 sidx segment 计算
    double maxDuration = 0.0;
    for (const auto& [codecId, track] : tracks_) {
        double timeScale = static_cast<double>(track->mdhd ? track->mdhd->timeScale : 1);
        uint64_t totalTicks = 0;
        // 从 sidx segment 累加
        for (const auto& seg : track->sidxSegments) {
            totalTicks = std::max(totalTicks, seg.startTime + seg.duration);
        }
        // 从 fragment sample 累加
        if (totalTicks == 0 && track->fragmentDurationTicks > 0) {
            totalTicks = track->fragmentDurationTicks;
        }
        if (totalTicks > 0) {
            double trackDuration = static_cast<double>(totalTicks) / timeScale;
            maxDuration = std::max(maxDuration, trackDuration);
        }
    }
    if (maxDuration > 0.0) {
        return CTime(maxDuration);
    }
    return totalDuration_;
}

void Mp4Demuxer::flush() noexcept {
    isCompleted_ = false;
    if (buffer_) {
        buffer_->reset();
    }
    for (auto& [codecId, track] : tracks_) {
        track->reset();
    }
}

void Mp4Demuxer::close() noexcept {
    tracks_.clear();
    buffer_.reset();
    rootBox_.reset();
    reset();
}

Mp4Demuxer::~Mp4Demuxer() {
    tracks_.clear();
    buffer_.reset();
    rootBox_.reset();
    IDemuxer::reset();
}

void recursiveDescription(const BoxRefPtr& ptr, const std::string& prefix, std::ostringstream& ss) {
    if (ptr == nullptr) {
        return;
    }
    ss << ptr->description(prefix);
    for (auto& child : ptr->childs) {
        recursiveDescription(child, prefix + "    ", ss);
    }
}

std::string Mp4Demuxer::description() const noexcept {
    if (!rootBox_) {
        return "";
    }
    std::ostringstream ss;
    recursiveDescription(rootBox_, "", ss);
    return ss.str();
}

std::shared_ptr<AudioFrameInfo> Mp4Demuxer::makeAudioFrameInfo() const noexcept {
    auto info = std::make_shared<AudioFrameInfo>();
    info->bitsPerSample = audioInfo_->bitsPerSample;
    info->channels = audioInfo_->channels;
    info->sampleRate = audioInfo_->sampleRate;
    return info;
}

std::shared_ptr<VideoFrameInfo> Mp4Demuxer::makeVideoFrameInfo() const noexcept {
    auto info = std::make_shared<VideoFrameInfo>();
    info->width = videoInfo_->width;
    info->height = videoInfo_->height;
    return info;
}

DemuxerResult Mp4Demuxer::parseData(DataPacket& packet) noexcept {
    if (packet.empty() || !isOpened_) {
        return {DemuxerResultCode::Failed, AVFramePtrArray(), AVFramePtrArray()};
    }
    if (!buffer_->append(static_cast<uint64_t>(packet.offset), std::move(packet.data))) {
        LogE("parseData: buffer append failed, offset:{}", packet.offset);
        return {DemuxerResultCode::InvalidData, AVFramePtrArray(), AVFramePtrArray()};
    }
    auto currentOffset = buffer_->pos();
    DemuxerResult result;
    while (!buffer_->empty()) {
        uint64_t offset = INT64_MAX;  // To find the earliest starting track
        std::shared_ptr<TrackContext> parseTrack;
        for (const auto& track : std::views::values(tracks_)) {
            uint64_t tOffset = INT64_MAX;
            if (!track->isInRange(*buffer_, &tOffset)) {
                continue;
            } else if (tOffset < offset && parseTrack != track) {
                parseTrack = track;
                offset = tOffset;
            }
        }
        if (!parseTrack) {
            bool advanced = false;
            for (const auto& track : std::views::values(tracks_)) {
                if (track->isCompleted || track->isFragmented) {
                    continue;
                }
                auto start = track->stco->chunkOffsets[track->chunkLogicIndex] +
                             static_cast<uint64_t>(track->sampleOffset);
                auto end = start + track->stsz->sampleSizes[track->stszSampleSizeIndex];
                if (end < buffer_->pos()) {
                    track->calcIndex();
                    advanced = true;
                }
            }
            if (advanced) {
                continue;  // 跳过后被跳过的 sample，重新找在范围内的 track
            }
            break;
        }
        if (parseTrack->type == TrackType::Audio) {
            parseTrack->parseData(*buffer_, makeAudioFrameInfo(), result.audioFrames);
        } else if (parseTrack->type == TrackType::Video) {
            parseTrack->parseData(*buffer_, makeVideoFrameInfo(), result.videoFrames);
        }
    }
    if (!result.audioFrames.empty() || !result.videoFrames.empty()) {
        LogI("parseData: parsedLength {} - {}, audioFrameCount:{}, videoFrameCount:{}",
             currentOffset,
             buffer_->pos(),
             result.audioFrames.size(),
             result.videoFrames.size());
        buffer_->shrink();
    }

    isCompleted_ = isCompleted();
    if (isCompleted_) {
        result.resultCode = DemuxerResultCode::FileEnd;
        LogI("demux completed, sending FileEnd");
    }
    return result;
}

bool Mp4Demuxer::isCompleted() const noexcept {
    bool isCompleted = true;
    for (const auto& track : std::views::values(tracks_)) {
        if (!track->isCompleted) {
            isCompleted = false;
        }
    }
    if (!isCompleted && !isFragmented_) {
        auto mediaDataBox = rootBox_->getChild("mdat");
        if (mediaDataBox &&
            buffer_->pos() >= (mediaDataBox->info.size + mediaDataBox->info.start)) {
            isCompleted = true;
        }
    }
    return isCompleted;
}

uint64_t Mp4Demuxer::getSeekToPos(double time) noexcept {
    LogI("getSeekToPos: time:{:.3f}s", time);
    uint64_t fallbackOffset = 0;
    if (headerInfo_) {
        auto mdatBox = rootBox_ ? rootBox_->getChild("mdat") : nullptr;
        const uint64_t headerSize = mdatBox ? mdatBox->info.headerSize : 8;
        fallbackOffset = headerInfo_->headerLength + headerSize;
    }

    if (isFragmented_) {
        uint64_t minOffset = UINT64_MAX;
        for (const auto& track : std::views::values(tracks_)) {
            if (!track->sidxSegments.empty()) {
                const auto timeScale = static_cast<double>(track->mdhd->timeScale);
                const uint64_t targetTicks = static_cast<uint64_t>(time * timeScale);
                uint64_t segOffset = 0;
                for (const auto& seg : track->sidxSegments) {
                    if (seg.startTime + seg.duration > targetTicks) {
                        segOffset = seg.offset;
                        break;
                    }
                }
                if (segOffset == 0 && !track->sidxSegments.empty()) {
                    segOffset = track->sidxSegments.back().offset;
                }
                minOffset = std::min(minOffset, segOffset);
            } else if (!track->fragSamples.empty()) {
                const auto timeScale = static_cast<double>(track->mdhd->timeScale);
                const int64_t targetTicks = static_cast<int64_t>(time * timeScale);
                uint64_t bestOffset = track->fragSamples.front().offset;
                for (const auto& s : track->fragSamples) {
                    if (s.dts >= targetTicks) {
                        if (track->type == TrackType::Video && !s.isKeyFrame) {
                            continue;
                        }
                        bestOffset = s.offset;
                        break;
                    }
                }
                minOffset = std::min(minOffset, bestOffset);
            }
        }
        return minOffset == UINT64_MAX ? fallbackOffset : minOffset;
    }

    uint64_t videoOffset = fallbackOffset;
    uint64_t audioOffset = fallbackOffset;
    for (const auto& track : std::views::values(tracks_)) {
        if (track->type == TrackType::Video) {
            videoOffset = track->getSeekPos(time);
        } else if (track->type == TrackType::Audio) {
            audioOffset = track->getSeekPos(time);
        }
    }
    LogI("seek to time, audio offset:{}, video offset:{}", audioOffset, videoOffset);
    return std::min(audioOffset, videoOffset);
}

void Mp4Demuxer::seekPos(uint64_t pos) noexcept {
    LogI("seekPos: pos:{}", pos);
    IDemuxer::seekPos(pos);
    for (auto& track : std::views::values(tracks_)) {
        if (track->isFragmented) {
            track->isCompleted = false;
            if (track->fragSamples.empty()) {
                track->fragSampleIndex = 0;
                continue;
            }
            auto it = std::lower_bound(track->fragSamples.begin(),
                                       track->fragSamples.end(),
                                       pos,
                                       [](const TrackContext::FragSample& s, uint64_t p) {
                                           return s.offset < p;
                                       });
            if (it != track->fragSamples.begin()) {
                --it;
            }
            if (track->type == TrackType::Video) {
                while (it != track->fragSamples.end() && !it->isKeyFrame) {
                    ++it;
                }
            }
            track->fragSampleIndex =
                static_cast<uint32_t>(std::distance(track->fragSamples.begin(), it));
        } else {
            track->seek(pos);
        }
    }
}

namespace {

uint32_t resolveSampleDuration(const TrunSample& s,
                               uint32_t trunFlags,
                               const std::shared_ptr<BoxTfhd>& tfhd,
                               const std::shared_ptr<BoxTrex>& trex) noexcept {
    if (trunFlags & BoxTrun::kSampleDurationPresent) {
        return s.duration;
    }
    if (tfhd->flags & BoxTfhd::kDefaultSampleDurationPresent) {
        return tfhd->defaultSampleDuration;
    }
    return trex ? trex->defaultSampleDuration : 0;
}

uint32_t resolveSampleSize(const TrunSample& s,
                           uint32_t trunFlags,
                           const std::shared_ptr<BoxTfhd>& tfhd,
                           const std::shared_ptr<BoxTrex>& trex) noexcept {
    if (trunFlags & BoxTrun::kSampleSizePresent) {
        return s.size;
    }
    if (tfhd->flags & BoxTfhd::kDefaultSampleSizePresent) {
        return tfhd->defaultSampleSize;
    }
    return trex ? trex->defaultSampleSize : 0;
}

uint32_t resolveSampleFlags(const TrunSample& s,
                            uint32_t i,
                            uint32_t trunFlags,
                            const std::shared_ptr<BoxTrun>& trun,
                            const std::shared_ptr<BoxTfhd>& tfhd,
                            const std::shared_ptr<BoxTrex>& trex) noexcept {
    if (trunFlags & BoxTrun::kSampleFlagsPresent) {
        return s.flags;
    }
    if (i == 0 && (trunFlags & BoxTrun::kFirstSampleFlagsPresent)) {
        return trun->firstSampleFlags;
    }
    if (tfhd->flags & BoxTfhd::kDefaultSampleFlagsPresent) {
        return tfhd->defaultSampleFlags;
    }
    return trex ? trex->defaultSampleFlags : 0;
}

}  // namespace

void TrackContext::appendFragmentSamples(const BoxRefPtr& moofBox, uint64_t moofOffset) noexcept {
    for (const auto& trafChild : moofBox->childs) {
        if (trafChild->info.symbol != "traf") {
            continue;
        }
        auto tfhdBox = std::dynamic_pointer_cast<BoxTfhd>(trafChild->getChild("tfhd"));
        if (!tfhdBox || tfhdBox->trackId != trackId) {
            continue;
        }

        uint64_t baseOffset = moofOffset;
        if (tfhdBox->flags & BoxTfhd::kBaseDataOffsetPresent) {
            baseOffset = tfhdBox->baseDataOffset;
        }

        int64_t baseDecodeTime = 0;
        if (auto tfdtBox = std::dynamic_pointer_cast<BoxTfdt>(trafChild->getChild("tfdt"))) {
            baseDecodeTime = static_cast<int64_t>(tfdtBox->baseMediaDecodeTime);
        } else if (!fragSamples.empty()) {
            const auto& last = fragSamples.back();
            baseDecodeTime = last.dts + static_cast<int64_t>(last.duration);
        }
        baseDecodeTime -= elstMediaTimeOffset;

        for (const auto& trafChild2 : trafChild->childs) {
            if (trafChild2->info.symbol != "trun") {
                continue;
            }
            auto trunBox = std::dynamic_pointer_cast<BoxTrun>(trafChild2);
            if (!trunBox) {
                continue;
            }

            uint64_t dataOffset = baseOffset;
            if (trunBox->flags & BoxTrun::kDataOffsetPresent) {
                dataOffset =
                    baseOffset + static_cast<uint64_t>(static_cast<int64_t>(trunBox->dataOffset));
            }

            int64_t currentDts = baseDecodeTime;
            for (uint32_t i = 0; i < trunBox->samples.size(); ++i) {
                const auto& s = trunBox->samples[i];
                const uint32_t dur = resolveSampleDuration(s, trunBox->flags, tfhdBox, trex);
                const uint32_t sz = resolveSampleSize(s, trunBox->flags, tfhdBox, trex);
                const uint32_t sampleFlags =
                    resolveSampleFlags(s, i, trunBox->flags, trunBox, tfhdBox, trex);
                const int32_t cto = (trunBox->flags & BoxTrun::kSampleCompositionTimeOffsetPresent)
                                        ? s.compositionTimeOffset
                                        : 0;

                FragSample fs;
                fs.offset = dataOffset;
                fs.size = sz;
                fs.duration = dur;
                fs.flags = sampleFlags;
                fs.compositionTimeOffset = cto;
                fs.dts = currentDts;
                fs.pts = currentDts + static_cast<int64_t>(cto);
                fs.isKeyFrame = !(sampleFlags & BoxTrun::kSampleIsNonSync);
                fragSamples.push_back(fs);

                dataOffset += sz;
                currentDts += static_cast<int64_t>(dur);
            }
            baseDecodeTime = currentDts;
        }
    }
    // 更新 fragment 总 duration（取最大 dts + duration）
    if (!fragSamples.empty()) {
        uint64_t lastEnd =
            static_cast<uint64_t>(fragSamples.back().dts + fragSamples.back().duration);
        fragmentDurationTicks = std::max(fragmentDurationTicks, lastEnd);
    }
    isCompleted = fragSamples.empty();
}

bool Mp4Demuxer::parseMoofBox(Buffer& /*buffer*/, const BoxRefPtr& moofBox) noexcept {
    const uint64_t moofOffset = moofBox->info.start;
    for (auto& [codecId, track] : tracks_) {
        if (track->isFragmented) {
            track->appendFragmentSamples(moofBox, moofOffset);
            track->isCompleted = false;
        }
    }
    return true;
}

void Mp4Demuxer::parseMoofChildren(Buffer& buffer, const BoxRefPtr& moofBox) noexcept {
    std::stack<BoxRefPtr> parentStack;
    parentStack.push(moofBox);
    while ((buffer.pos() - moofBox->info.start) < moofBox->info.size) {
        auto box = Box::createBox(buffer);
        if (!box) {
            break;
        }
        box->decode(buffer);
        parentStack.top()->append(box);
        if (isContainerBox(box->info.symbol)) {
            parentStack.push(box);
            continue;
        }
        const auto skipOffset =
            static_cast<int64_t>(box->info.start + box->info.size - buffer.pos());
        if (!buffer.skip(skipOffset)) {
            break;
        }
        const auto endPos = box->info.size + box->info.start;
        while (!parentStack.empty()) {
            if (endPos >= (parentStack.top()->info.start + parentStack.top()->info.size)) {
                parentStack.pop();
            } else {
                break;
            }
        }
    }
}

}  // namespace slark
