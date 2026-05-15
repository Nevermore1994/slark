//
//  Mp4Demuxer.cpp
//  slark
//
//  Created by Nevermore
//

#include "Mp4Demuxer.h"

#include "IDemuxer.h"
#include "MediaUtil.h"

#include <optional>
#include <ranges>
#include <stack>

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
    };
    return std::any_of(kContainerBox.begin(), kContainerBox.end(), [symbol](const auto& view) {
        return view == symbol;
    });
}

void TrackContext::init() noexcept {
    if (type == TrackType::Video && ctts) {
        // Set pts to 0 and calculate the initial value of dts, which may be a negative number.
        dts = pts - ctts->entrys[0].sampleOffset;
        cttsEntrySampleIndex = 0;
    }
}

uint64_t TrackContext::getSeekPos(double targetTime) const noexcept {
    if (!stts || !stsc || !stco || !stsz) {
        return 0;
    }

    // 第一步：根据时间找到 sample index
    uint32_t sampleIndex = findSampleIndexByTime(targetTime);

    // 第二步：视频轨道对齐到最近关键帧
    uint32_t seekSampleIndex = sampleIndex;
    if (type == TrackType::Video && stss) {
        const auto& keyIndexes = stss->keyIndexs;
        auto it = std::lower_bound(keyIndexes.begin(), keyIndexes.end(), sampleIndex);
        seekSampleIndex = it != keyIndexes.end() ? *it : keyIndexes.back();
    }

    // 第三步：找到 chunk index 和 chunk 内的 sample 偏移
    auto chunkResult = findChunkBySampleIndex(seekSampleIndex);
    if (!chunkResult.has_value()) {
        return 0;
    }
    auto [chunkIndex, firstSampleInChunk] = chunkResult.value();

    if (chunkIndex >= stco->chunkOffsets.size()) {
        LogE("chunkIndex out of range");
        return 0;
    }
    const uint64_t kChunkOffset = stco->chunkOffsets[chunkIndex];
    uint64_t sampleOffsetInChunk = 0;
    for (uint32_t i = 0; i < seekSampleIndex - firstSampleInChunk; ++i) {
        sampleOffsetInChunk += stsz->sampleSizes[firstSampleInChunk - 1 + i];
    }
    const auto kPos = kChunkOffset + sampleOffsetInChunk;
    LogI("[seek info] {} keySampleIndex:{}, time:{}, pos:{}",
         type == TrackType::Video ? "video" : "audio",
         seekSampleIndex,
         targetTime,
         kPos);
    return kPos;
}

uint32_t TrackContext::findSampleIndexByTime(double targetTime) const noexcept {
    uint32_t sampleIndex = 0;
    const auto kTimeScale = static_cast<double>(mdhd->timeScale);
    double currentTime = 0.0;
    for (const auto& entry : stts->entrys) {
        const auto kEntryDuration =
            static_cast<double>(entry.sampleCount * entry.sampleDelta) / kTimeScale;
        if (currentTime + kEntryDuration > targetTime) {
            const auto kOffset =
                static_cast<uint32_t>((targetTime - currentTime) * kTimeScale / entry.sampleDelta);
            sampleIndex += std::min(kOffset, entry.sampleCount - 1);
            break;
        }
        currentTime += kEntryDuration;
        sampleIndex += entry.sampleCount;
    }
    return sampleIndex == 0 ? 1 : sampleIndex;
}

std::optional<std::pair<uint32_t, uint32_t>> TrackContext::findChunkBySampleIndex(
    uint32_t seekSampleIndex) const noexcept {
    uint32_t sampleCount = 0;
    for (size_t i = 0; i < stsc->entrys.size(); ++i) {
        const auto& entry = stsc->entrys[i];
        const uint32_t kNextFirstChunk = (i + 1 < stsc->entrys.size())
                                             ? stsc->entrys[i + 1].firstChunk
                                             : static_cast<uint32_t>(stco->chunkOffsets.size()) + 1;
        for (uint32_t chunk = entry.firstChunk; chunk < kNextFirstChunk; ++chunk) {
            const uint32_t kChunkSampleCount = entry.samplesPerChunk;
            if (seekSampleIndex <= sampleCount + kChunkSampleCount) {
                return std::make_pair(chunk - 1, sampleCount + 1);
            }
            sampleCount += kChunkSampleCount;
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
    const auto kChunkIndex = static_cast<uint32_t>(std::distance(chunks.begin(), it));

    // 通过 stsc 表直接计算 chunk 前的累计 sample 数，O(stsc entries) 通常极小
    uint32_t sampleBeforeChunk = 0;
    uint32_t samplesPerChunk = 1;
    const auto& entries = stsc->entrys;
    for (size_t i = 0; i < entries.size(); ++i) {
        const uint32_t kFirstChunk = entries[i].firstChunk - 1;
        const uint32_t kNextFirstChunk = (i + 1 < entries.size())
                                             ? entries[i + 1].firstChunk - 1
                                             : static_cast<uint32_t>(chunks.size());
        if (kChunkIndex < kNextFirstChunk) {
            sampleBeforeChunk += (kChunkIndex - kFirstChunk) * entries[i].samplesPerChunk;
            samplesPerChunk = entries[i].samplesPerChunk;
            break;
        }
        sampleBeforeChunk += (kNextFirstChunk - kFirstChunk) * entries[i].samplesPerChunk;
    }

    // 在 chunk 内线性扫描找到精确 sample（samplesPerChunk 通常很小）
    uint32_t sampleInChunk = 0;
    uint64_t sampleStart = chunks[kChunkIndex];
    while (sampleInChunk < samplesPerChunk) {
        const uint32_t kSampleIdx = sampleBeforeChunk + sampleInChunk;
        if (kSampleIdx >= stsz->sampleSizes.size()) {
            break;
        }
        const uint64_t kSampleEnd = sampleStart + stsz->sampleSizes[kSampleIdx];
        if (kSampleEnd > pos) {
            break;
        }
        sampleStart = kSampleEnd;
        ++sampleInChunk;
    }

    uint32_t sampleIndex = sampleBeforeChunk + sampleInChunk + 1; // 1-based
    if (stss) {
        const auto& keyIndexs = stss->keyIndexs;
        auto keyIt = std::lower_bound(keyIndexs.begin(), keyIndexs.end(), sampleIndex);
        sampleIndex = keyIt != keyIndexs.end() ? *keyIt : keyIndexs.back();
    }

    // 直接通过 stts 累加计算目标 sample 的 dts，避免逐帧推进
    reset();
    seekToSample(sampleIndex);

    const auto kFinalPos =
        stco->chunkOffsets[chunkLogicIndex] + static_cast<uint64_t>(sampleOffset);
    LogI("[seek info] {} sampleIndex:{}, dts:{}, pos:{}",
         type == TrackType::Video ? "video" : "audio",
         index,
         dts,
         kFinalPos);
}

namespace {

// 在 RLE 表中定位第 targetIndex 个元素，返回 {entryIndex, offsetInEntry}，未找到返回 nullopt
template <typename Entry, typename CountFn>
std::optional<std::pair<uint32_t, uint32_t>> seekInRle(const std::vector<Entry>& entries,
                                                       uint32_t targetIndex,
                                                       CountFn countFn) noexcept {
    uint32_t accumulated = 0;
    for (size_t i = 0; i < entries.size(); ++i) {
        const uint32_t kCount = countFn(entries[i]);
        if (accumulated + kCount > targetIndex) {
            return std::make_pair(static_cast<uint32_t>(i), targetIndex - accumulated);
        }
        accumulated += kCount;
    }
    return std::nullopt;
}

} // namespace

void TrackContext::seekToSample(uint32_t targetSampleIndex) noexcept {
    const uint32_t kIdx = targetSampleIndex > 0 ? targetSampleIndex - 1 : 0;

    // stsc: 定位 chunk 和 chunk 内偏移
    uint32_t stscAccumulated = 0;
    for (size_t i = 0; i < stsc->entrys.size(); ++i) {
        const auto& entry = stsc->entrys[i];
        const uint32_t kFirstChunk = entry.firstChunk - 1;
        const uint32_t kNextFirstChunk = (i + 1 < stsc->entrys.size())
                                             ? stsc->entrys[i + 1].firstChunk - 1
                                             : static_cast<uint32_t>(stco->chunkOffsets.size());
        const uint32_t kEntryTotal = (kNextFirstChunk - kFirstChunk) * entry.samplesPerChunk;
        if (stscAccumulated + kEntryTotal > kIdx) {
            const uint32_t kRemaining = kIdx - stscAccumulated;
            chunkLogicIndex = kFirstChunk + kRemaining / entry.samplesPerChunk;
            entrySampleIndex = kRemaining % entry.samplesPerChunk;
            stscEntryIndex = static_cast<uint32_t>(i);
            sampleOffset = 0;
            for (uint32_t s = 0; s < entrySampleIndex; ++s) {
                sampleOffset +=
                    stsz->sampleSizes[stscAccumulated +
                                      (chunkLogicIndex - kFirstChunk) * entry.samplesPerChunk + s];
            }
            stszSampleSizeIndex = kIdx;
            break;
        }
        stscAccumulated += kEntryTotal;
    }

    // stts: 定位 dts
    dts = 0;
    if (auto r = seekInRle(stts->entrys, kIdx, [](const auto& e) {
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
    } else if (auto r = seekInRle(ctts->entrys, kIdx, [](const auto& e) {
                   return e.sampleCount;
               })) {
        cttsEntryIndex = r->first;
        cttsEntrySampleIndex = r->second;
        pts = dts + ctts->entrys[cttsEntryIndex].sampleOffset;
    }

    index = targetSampleIndex;
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
    init();
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
    // 先取当前 entry 的 delta，再推进 entry index，避免引用失效后取到错误值
    const auto kDelta = stts->entrys[sttsEntryIndex].sampleDelta;
    sttsEntrySampleIndex++;
    if (sttsEntrySampleIndex >= stts->entrys[sttsEntryIndex].sampleCount) {
        sttsEntrySampleIndex = 0;
        if (sttsEntryIndex + 1 < stts->entrys.size()) {
            sttsEntryIndex++;
        }
    }
    dts += kDelta;
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
    const auto& sttsEntry = stts->entrys[sttsEntryIndex];
    const uint32_t kDuration =
        static_cast<uint32_t>(static_cast<double>(sttsEntry.sampleDelta) /
                              static_cast<double>(frame->timeScale) * 1000.0);

    const uint32_t kHeaderSize = naluByteSize + 1;
    const auto kRawView = data->view();
    uint32_t pos = 0;

    while (pos + kHeaderSize <= kRawView.length()) {
        uint32_t naluSize = 0;
        auto sizeView = kRawView.substr(pos, 4);
        sizeView.readNumberBE(naluSize, kHeaderSize);
        const uint32_t kTotalSize = naluSize + kHeaderSize;
        if (pos + kTotalSize > kRawView.length()) {
            break; // 防止越界
        }

        uint8_t naluHeader = 0;
        auto naluHeaderView = kRawView.substr(pos + 4, 1);
        naluHeaderView.readNumberLE(naluHeader, 1);

        const auto [shouldProcess, frameType] = classifier(naluHeader);
        if (!shouldProcess) {
            pos += kTotalSize;
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

        pos += kTotalSize;
        const bool kIsLast = (pos >= kRawView.length());

        // 最后一个 NALU 直接 move frame，其余 copy；data 统一切片
        auto newFrame = kIsLast ? std::move(frame) : frame->copy();
        newFrame->duration = kDuration;
        newFrame->info = std::move(info);
        newFrame->data = std::make_unique<Data>(kRawView.substr(pos - kTotalSize, kTotalSize));
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
                               const uint8_t kNaluType = naluHeader & 0x1f;
                               if (kNaluType != 5 && kNaluType != 1) {
                                   return {false, VideoFrameType::IFrame};
                               }
                               if (kNaluType == 5) {
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
                               const uint8_t kNaluType = (naluHeader >> 1) & 0x3f;
                               if (kNaluType > 21) {
                                   return {false, VideoFrameType::IFrame};
                               }
                               if (kNaluType >= 16 && kNaluType <= 21) {
                                   return {true, VideoFrameType::IFrame};
                               }
                               return {true, VideoFrameType::PFrame};
                           });
}

void TrackContext::parseData(Buffer& buffer,
                             std::shared_ptr<FrameInfo> frameInfo,
                             AVFramePtrArray& packets) {
    uint64_t start = 0;
    if (!isInRange(buffer, &start)) {
        return;
    }
    auto size = stsz->sampleSizes[stszSampleSizeIndex];
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

bool Mp4Demuxer::probeMoovBox(Buffer& buffer, int64_t& start, uint32_t& size) noexcept {
    auto view = buffer.view();
    auto pos = view.find("moov");
    if (pos == std::string_view::npos) {
        return false;
    }
    auto boxStart = pos - 4;
    start = static_cast<int64_t>(boxStart + buffer.offset()); // size start pos
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
        auto [codecId, mediaBoxName] = stsdBox->getCodecId();
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
    [[maybe_unused]] bool res = true;
    std::shared_ptr<BoxMdhd> mdhdBox;
    while ((buffer.pos() - moovBox->info.start) < moovBox->info.size) {
        auto box = Box::createBox(buffer);
        if (!box) {
            LogE("parseMoovBox: createBox failed at pos:{}", buffer.pos());
            res = false;
            break;
        }
        box->decode(buffer);
        handleBoxInMoov(box, mdhdBox, parentBox);
        parentBox.top()->append(box);
        if (isContainerBox(box->info.symbol)) {
            parentBox.push(box);
            continue;
        }
        auto skipOffset = static_cast<int64_t>(box->info.start + box->info.size - buffer.pos());
        if (!buffer.skip(skipOffset)) {
            LogE("parseMoovBox: skip {} bytes failed, box:{}", skipOffset, box->info.symbol);
            res = false;
            break;
        }
        const auto kEndPos = box->info.size + box->info.start;
        while (!parentBox.empty()) {
            if (kEndPos >= (parentBox.top()->info.start + parentBox.top()->info.size)) {
                parentBox.pop();
            } else {
                break;
            }
        }
    }
    return res;
}

bool Mp4Demuxer::open(std::unique_ptr<Buffer>& buffer) noexcept {
    if (rootBox_ == nullptr) {
        BoxInfo info;
        info.start = 0;
        info.size = config_.fileSize;
        rootBox_ = std::make_shared<Box>(std::move(info));
    }
    auto res = false;
    while (!buffer->empty()) {
        auto box = Box::createBox(*buffer);
        if (!box) {
            break;
        }

        if (box->info.symbol == "moov") {
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
        } else {
            rootBox_->append(box);
        }

        const auto kEndPos = box->info.size + box->info.start;
        const auto kSkipOffset = kEndPos - buffer->pos();
        if (!buffer->skip(static_cast<int64_t>(kSkipOffset))) {
            if (box->info.symbol != "mdat") {
                res = false;
            }
            break;
        }
    }

    if (!res) {
        LogE("open: failed, reset read pos");
        buffer->resetReadPos();
    } else {
        LogI("open: moov parsed, fileSize:{}", config_.fileSize);
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
             // co64 用于大文件（>4GB），将其 offsets 桥接到 stco 兼容结构
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
    for (auto& child : track->stbl->childs) {
        if (auto it = kBoxSetters.find(child->info.symbol); it != kBoxSetters.end()) {
            it->second(child, *track);
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
    // TODO: support more format
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

    auto sttsBox = track->stts;
    const auto kDelta = sttsBox->entrys.front().sampleDelta;
    if (kDelta > 0) {
        videoInfo_->fps = static_cast<uint16_t>(videoInfo_->timeScale / kDelta);
    } else {
        const auto kSampleSize = static_cast<double>(sttsBox->sampleSize());
        videoInfo_->fps = static_cast<uint16_t>(ceil(kSampleSize / totalDuration_.second()));
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
    LogI("initData: duration:{:.3f}s, tracks:{}",
         totalDuration_.second(),
         static_cast<int32_t>(tracks_.size()));
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
    DemuxerResult result;
    while (!buffer_->empty()) {
        uint64_t offset = INT64_MAX; // To find the earliest starting track
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
            break;
        }
        if (parseTrack->type == TrackType::Audio) {
            parseTrack->parseData(*buffer_, makeAudioFrameInfo(), result.audioFrames);
        } else if (parseTrack->type == TrackType::Video) {
            parseTrack->parseData(*buffer_, makeVideoFrameInfo(), result.videoFrames);
        }
    }
    if (!result.audioFrames.empty() || !result.videoFrames.empty()) {
        buffer_->shrink();
    }

    isCompleted_ = isCompleted();
    if (isCompleted_) {
        LogI("demux completed");
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
    if (!isCompleted) {
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
    // fallback: mdat 数据起始位置（跳过 box header）
    uint64_t fallbackOffset = 0;
    if (headerInfo_) {
        auto mdatBox = rootBox_ ? rootBox_->getChild("mdat") : nullptr;
        const uint64_t kHeaderSize = mdatBox ? mdatBox->info.headerSize : 8;
        fallbackOffset = headerInfo_->headerLength + kHeaderSize;
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
        track->seek(pos);
    }
}

} // namespace slark
