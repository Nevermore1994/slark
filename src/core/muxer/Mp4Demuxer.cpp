//
//  Mp4Demuxer.cpp
//  slark
//
//  Created by Nevermore
//

#include <stack>
#include <ranges>
#include "IDemuxer.h"
#include "Mp4Demuxer.h"
#include "MediaBase.h"

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

uint64_t TrackContext::getSeekPos(long double targetTime) const noexcept {
    if (type != TrackType::Video) {
        return 0;
    }
    uint32_t sampleIndex = 0;
    auto timeScale = static_cast<long double>(mdhd->timeScale);
    double currentDTS = 0;
    for (auto & entry : stts->entrys) {
        double entryDuration = entry.sampleCount * entry.sampleDelta;
        if (currentDTS + entryDuration >= targetTime) {
            sampleIndex += static_cast<uint32_t>((targetTime - currentDTS) / (static_cast<long double>(entry.sampleDelta) / timeScale));
            break;
        }
        currentDTS += entryDuration;
        sampleIndex += entry.sampleCount;
    }
    if (sampleIndex == 0) {
        sampleIndex = 1;
    }
    const auto& indexs = stss->keyIndexs;
    auto nowKeyIndex = static_cast<uint32_t>(std::distance(indexs.begin(), std::lower_bound(indexs.begin(), indexs.end(), sampleIndex)));
    if (nowKeyIndex == indexs.size()) {
        //last key frame
        nowKeyIndex = static_cast<uint32_t>(indexs.size() - 1);
    }
    while (nowKeyIndex < indexs.size() && indexs[nowKeyIndex] > sampleIndex) {
        nowKeyIndex--;
    }
    if (nowKeyIndex >= indexs.size()) {
        LogE("key index error!");
        return 0;
    }
    uint32_t keyFrameIndex = indexs[nowKeyIndex];
    
    uint32_t chunkIndex = 0;
    uint32_t stscIndex = 0;
    uint32_t chunkSampleOffset = 0;
    uint32_t sampleCount = 0;
    while(chunkIndex < keyFrameIndex) {
        auto chunkSampleCount = stsc->entrys[stscIndex].samplesPerChunk;
        if (stscIndex + 1 < stsc->entrys.size()) {
            auto chunkCount = stsc->entrys[stscIndex + 1].firstChunk - stsc->entrys[stscIndex].firstChunk;
            if (keyFrameIndex < sampleCount + (chunkCount * chunkSampleCount)) {
                while (keyFrameIndex < sampleCount + chunkSampleCount) {
                    sampleCount += chunkSampleCount;
                    chunkSampleOffset += chunkSampleCount;
                    chunkIndex++;
                }
                chunkSampleOffset += keyFrameIndex - sampleCount;
                break;
            } else {
                sampleCount += chunkCount * chunkSampleCount;
                chunkIndex += chunkCount;
                chunkSampleOffset = 0;
            }
        } else {
            chunkIndex += keyFrameIndex / chunkSampleCount;
            sampleCount += keyFrameIndex / chunkSampleCount;
            chunkSampleOffset = keyFrameIndex - (keyFrameIndex / chunkSampleCount);
            break;
        }
    }
    
    uint64_t chunkOffset = stco->chunkOffsets[chunkIndex - 1];
    uint64_t sampleOffsetInChunk = 0;
    for (uint32_t i = 0; i < chunkSampleOffset; ++i) {
        sampleOffsetInChunk += stsz->sampleSizes[keyFrameIndex - chunkSampleOffset + i];
    }
    LogI("[seek info] keyindex:{}, dts:{}, time:{}, pos:{}, chunk:{}", keyFrameIndex, currentDTS, targetTime, chunkOffset + sampleOffsetInChunk, chunkIndex);
    return chunkOffset + sampleOffsetInChunk;
}

void TrackContext::seek(uint64_t pos) noexcept {
    if (!stco || !stsz || !stsc || !stts) {
        return;
    }
    
    const auto& chunks = stco->chunkOffsets;
    auto it = std::lower_bound(chunks.begin(), chunks.end(), pos);
    if (it == chunks.end()) {
        return;
    }

    reset();
    auto chunkIndex = std::distance(chunks.begin(), it);
    uint32_t sampleIndex = 0;
    const auto& stscEntrys = stsc->entrys;
    uint32_t entryIndex = 0;
    while (entryIndex < stscEntrys.size() && stscEntrys[entryIndex].firstChunk < chunkIndex + 1) {
        if (entryIndex + 1 < stscEntrys.size()) {
            auto count = stscEntrys[entryIndex+1].firstChunk - stscEntrys[entryIndex].firstChunk;
            sampleIndex += count * stscEntrys[entryIndex].samplesPerChunk;
        } else {
            sampleIndex += (chunkIndex + 1 - stscEntrys[entryIndex].firstChunk) * stscEntrys[entryIndex].samplesPerChunk;
        }
        entryIndex++;
    }
    if (sampleIndex == 0) {
        sampleIndex = 1;
    }
    if (stss) {
        const auto& indexs = stss->keyIndexs;
        auto nowKeyIndex = static_cast<uint32_t>(std::distance(indexs.begin(), std::lower_bound(indexs.begin(), indexs.end(), sampleIndex)));
        sampleIndex = indexs[nowKeyIndex];
    }
    index = sampleIndex - 1;
    for(uint64_t i = 0; i < index; i++) {
        calcIndex();
    }
    auto start = stco->chunkOffsets[chunkLogicIndex] + static_cast<uint64_t>(sampleOffset);
    while (start < pos) {
        index++;
        calcIndex();
        start = stco->chunkOffsets[chunkLogicIndex] + static_cast<uint64_t>(sampleOffset);
    }

    if (type == TrackType::Audio) {
        LogI("[seek info] audio sampleIndex:{}, dts:{}, pos:{}", index, dts, start);
    } else {
        LogI("[seek info] video sampleIndex:{}, dts:{}, pos:{}", index, dts, start);
    }
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
}

void TrackContext::calcIndex() noexcept {
    auto size = stsz->sampleSizes[stszSampleSizeIndex];
    auto& entry = stsc->entrys[stscEntryIndex];
    auto& sttsEntry = stts->entrys[sttsEntryIndex];
    dts += sttsEntry.sampleDelta;
    if (ctts) {
        pts = dts + ctts->entrys[cttsEntryIndex].sampleOffset;
    } else {
        pts = dts;
    }
    
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
    
    sttsEntrySampleIndex++;
    if (sttsEntrySampleIndex >= sttsEntry.sampleCount) {
        sttsEntrySampleIndex = 0;
        if (sttsEntryIndex + 1 < stts->entrys.size()) {
            sttsEntryIndex++;
        }
    }
    
    if (ctts) {
        cttsEntrySampleIndex++;
        if (cttsEntrySampleIndex > ctts->entrys[cttsEntryIndex].sampleCount) {
            if (cttsEntryIndex + 1 < ctts->entrys.size()) {
                cttsEntryIndex ++;
            }
            cttsEntrySampleIndex = 0;
        }
    }
    
    if (stscEntryIndex + 1 < stsc->entrys.size()) {
        if (chunkLogicIndex >= (stsc->entrys[stscEntryIndex + 1].firstChunk - 1)) {
            stscEntryIndex++;
        }
    }
}

bool TrackContext::isInRange(Buffer& buffer) const noexcept {
    if (stszSampleSizeIndex >= stsz->sampleSizes.size() || stscEntryIndex >= stsc->entrys.size()) {
        return false;
    }
    auto start = stco->chunkOffsets[chunkLogicIndex] + static_cast<uint64_t>(sampleOffset);
    auto end = start + stsz->sampleSizes[stszSampleSizeIndex];
    auto bufferStart = buffer.offset();
    auto bufferEnd = bufferStart + buffer.totalLength();
    if (bufferStart <= start && end <= bufferEnd) {
        return true;
    }
    return false;
}

bool TrackContext::isInRange(Buffer& buffer, uint64_t& offset) const noexcept {
    if (stszSampleSizeIndex >= stsz->sampleSizes.size() || stscEntryIndex >= stsc->entrys.size()) {
        return false;
    }
    auto start = stco->chunkOffsets[chunkLogicIndex] + static_cast<uint64_t>(sampleOffset);
    auto end = start + stsz->sampleSizes[stszSampleSizeIndex];
    auto bufferStart = buffer.offset();
    auto bufferEnd = bufferStart + buffer.totalLength();
    if (bufferStart <= start && end <= bufferEnd) {
        offset = start;
        return true;
    }
    return false;
}

AVFramePtrArray TrackContext::parseH264FrameData(AVFramePtr frame,
                                                 DataPtr data,
                                                 std::shared_ptr<VideoFrameInfo> frameInfo) {
    if (naluByteSize == 0) {
        auto avccBox = std::dynamic_pointer_cast<BoxAvcc>(stsd->getChild("avc1")->getChild("avcC"));
        if (avccBox) {
            naluByteSize = avccBox->naluByteSize;
        }
    }
    AVFramePtrArray frames;
    auto view = data->view();
    auto& sttsEntry = stts->entrys[sttsEntryIndex];
    frame->duration = static_cast<uint32_t>(static_cast<double>(sttsEntry.sampleDelta) / static_cast<double>(frame->timeScale) * 1000.0); //ms
    while (!view.empty()) {
        uint32_t naluSize = 0;
        auto sizeView = view.substr(0, 4);
        Util::readBE(sizeView, naluByteSize + 1, naluSize);
        uint8_t naluHeader = 0;
        auto naluHeaderView = view.substr(4, 1);
        Util::readByte(naluHeaderView, naluHeader);
        uint8_t naluType = naluHeader & 0x1f;
        uint32_t totalSize = naluSize + naluByteSize + 1; //naluSize + header size
        auto info = std::make_shared<VideoFrameInfo>();
        frameInfo->copy(info);
        if (naluType == 5 || naluType == 1) {
            if (naluType == 5) {
                info->isIDRFrame = true;
                keyIndex = frame->index;
            } else {
                info->isIDRFrame = false;
                info->keyIndex = keyIndex;
            }
            DataPtr frameData;
            if (totalSize == data->length) {
                frameData = std::move(data);
                view = {};
                data.reset();
            } else {
                frameData = std::make_unique<Data>(view.substr(0, totalSize));
                view = view.substr(totalSize);
            }
            // parseSliceType need skip header
            auto [firstMBInSlice, sliceType] = parseSliceType(frameData->view().substr(4));
            if (sliceType == 0 || sliceType == 5) {
                info->frameType = VideoFrameType::PFrame;
            } else if (sliceType == 1 || sliceType == 6) {
                info->frameType = VideoFrameType::BFrame;
            } else if (sliceType == 2 || sliceType == 7) {
                info->frameType = VideoFrameType::IFrame;
            }
            info->keyIndex = keyIndex;
            if (view.empty()) {
                frame->info = info;
                frame->data = std::move(frameData);
                frames.push_back(std::move(frame));
                frame.reset();
            } else {
                auto newFrame = frame->copy();
                newFrame->info = info;
                newFrame->data = std::move(frameData);
                frames.push_back(std::move(newFrame));
            }
        } else {
            //don't care
            view = view.substr(totalSize);
        }
    }
    return frames;
}

void TrackContext::parseData(Buffer& buffer, std::shared_ptr<FrameInfo> frameInfo, AVFramePtrArray& packets)  {
    if (!isInRange(buffer)) {
        return;
    }
    auto start = stco->chunkOffsets[chunkLogicIndex] + static_cast<uint64_t>(sampleOffset);
    auto size = stsz->sampleSizes[stszSampleSizeIndex];
    //LogI("parse data start:{}, size:{}, pos:{}", start, size, buffer.pos());
    if (!buffer.skipTo(static_cast<int64_t>(start))) {
        return;
    }
    //LogI("skip to pos:{}, read pos:{}, offset:{}", buffer.pos(), buffer.readPos(), buffer.offset());
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
        if (codecId == CodecId::AVC) {
            auto frames = parseH264FrameData(std::move(frame),
                                             std::move(data),
                                             std::dynamic_pointer_cast<VideoFrameInfo>(frameInfo));
            std::for_each(frames.begin(), frames.end(), [&packets](auto& frame){
                packets.push_back(std::move(frame));
            });
        }
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
    start = static_cast<int64_t>(boxStart + buffer.offset()); //size start pos
    if (start < 0) {
        return false;
    }
    auto sizeView = view.substr(boxStart, 4);
    return Util::read4ByteBE(sizeView, size);
}

bool Mp4Demuxer::parseMoovBox(Buffer& buffer, const BoxRefPtr& moovBox) noexcept {
    std::stack<BoxRefPtr> parentBox;
    parentBox.push(moovBox);
    auto size = moovBox->info.size - moovBox->info.headerSize;
    if (!buffer.require(size)) {
        return false;
    }
    [[maybe_unused]] bool res  = true;
    std::shared_ptr<BoxMdhd> mdhdBox;
    while ((buffer.pos() - moovBox->info.start) < moovBox->info.size) {
        auto box = Box::createBox(buffer);
        if (!box) {
            res = false;
            break;
        }
        
        box->decode(buffer);
    
        if (box->info.symbol == "stsd") {
            auto stsdBox = std::dynamic_pointer_cast<BoxStsd>(box);
            SAssert(stsdBox != nullptr, "parse error");
            auto codecId = stsdBox->getCodecId();
            if (codecId == CodecId::Unknown) {
                LogI("Unknown codec!");
            } else {
                tracks_[codecId] = std::make_shared<TrackContext>();
                tracks_[codecId]->stbl = parentBox.top();
                tracks_[codecId]->mdhd = mdhdBox;
                tracks_[codecId]->type = stsdBox->isAudio ? TrackType::Audio : TrackType::Video;
                tracks_[codecId]->codecId = codecId;
            }
        } else if (box->info.symbol == "mdhd") {
            mdhdBox = std::dynamic_pointer_cast<BoxMdhd>(box);
        }
        
        parentBox.top()->append(box);
        if (isContainerBox(box->info.symbol)) {
            parentBox.push(box);
            continue;
        } else {
            auto endPos = box->info.size + box->info.start;
            auto skipOffset = static_cast<int64_t>(box->info.start + box->info.size - buffer.pos());
            if (!buffer.skip(skipOffset)) {
                res = false;
                break;
            }
            while(!parentBox.empty()) {
                auto topBox = parentBox.top();
                if (endPos >= (topBox->info.start + topBox->info.size)) {
                    parentBox.pop();
                } else {
                    break;
                }
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

        auto endPos = box->info.size + box->info.start;
        auto skipOffset = endPos - buffer->pos();
        if (!buffer->skip(static_cast<int64_t>(skipOffset))) {
            if (box->info.symbol != "mdat") {
                res = false;
            }
            break;
        }
    }
    
    if (!res) {
        buffer->resetReadPos();
    } else {
        initData();
    }
    return res;
}

void initTrack(std::shared_ptr<TrackContext>& track) {
    for (auto& child : track->stbl->childs) {
        if (child->info.symbol == "stsd") {
            track->stsd = std::dynamic_pointer_cast<BoxStsd>(child);
        } else if (child->info.symbol == "stts") {
            track->stts = std::dynamic_pointer_cast<BoxStts>(child);
        } else if (child->info.symbol == "stsz") {
            track->stsz = std::dynamic_pointer_cast<BoxStsz>(child);
        } else if (child->info.symbol == "stco") {
            track->stco = std::dynamic_pointer_cast<BoxStco>(child);
        } else if (child->info.symbol == "stsc") {
            track->stsc = std::dynamic_pointer_cast<BoxStsc>(child);
        } else if (child->info.symbol == "ctts") {
            track->ctts = std::dynamic_pointer_cast<BoxCtts>(child);
        } else if (child->info.symbol == "stss") {
            track->stss = std::dynamic_pointer_cast<BoxStss>(child);
        }
    }
}

void Mp4Demuxer::initData() noexcept {
    auto mvhdBox = std::dynamic_pointer_cast<BoxMvhd>(rootBox_->getChild("moov")->getChild("mvhd"));
    totalDuration_ = CTime(static_cast<double>(mvhdBox->duration) / static_cast<double>(mvhdBox->timeScale));
    for (auto& [codecId, track] : tracks_) {
        initTrack(track);
        auto stsdBox = track->stsd;
        if (!stsdBox) {
            LogI("get stsd error");
            continue;
        }
        if (track->type == TrackType::Audio) {
            audioInfo_ = std::make_shared<AudioInfo>();
            audioInfo_->channels = stsdBox->channelCount;
            audioInfo_->sampleRate = stsdBox->sampleRate;
            audioInfo_->bitsPerSample = stsdBox->sampleSize;
            if (stsdBox && track->codecId == CodecId::AAC) {
                auto esdsBox = std::dynamic_pointer_cast<BoxEsds>(stsdBox->getChild("mp4a")->getChild("esds"));
                if (esdsBox) {
                    audioInfo_->channels = esdsBox->audioSpecConfig.channelConfiguration;
                }
            }
            //TODO: support more format
            audioInfo_->mediaInfo = codecId == CodecId::AAC ? MEDIA_MIMETYPE_AUDIO_AAC : MEDIA_MIMETYPE_UNKNOWN;
            audioInfo_->timeScale = track->mdhd->timeScale;
        } else if (track->type == TrackType::Video) {
            videoInfo_ = std::make_shared<VideoInfo>();
            videoInfo_->width = stsdBox->width;
            videoInfo_->height = stsdBox->height;
            auto sttsBox = track->stts;
            auto delta = sttsBox->entrys.front().sampleDelta;
            videoInfo_->timeScale = track->mdhd->timeScale;
            if (delta > 0) {
                videoInfo_->fps = static_cast<uint16_t>(videoInfo_->timeScale / delta);
            } else {
                auto sampleSize = static_cast<double>(sttsBox->sampleSize());
                videoInfo_->fps = static_cast<uint16_t>(ceil(sampleSize / totalDuration_.second()));
            }
            if (codecId == CodecId::AVC) {
                videoInfo_->mediaInfo = MEDIA_MIMETYPE_VIDEO_AVC;
                auto avccBox = std::dynamic_pointer_cast<BoxAvcc>(stsdBox->getChild("avc1")->getChild("avcC"));
                if (avccBox) {
                    videoInfo_->sps = avccBox->sps.front();
                    videoInfo_->pps = avccBox->pps.front();
                    videoInfo_->naluHeaderLength = avccBox->naluByteSize + 1;
                }
            } else if (codecId == CodecId::HEVC) {
                videoInfo_->mediaInfo = MEDIA_MIMETYPE_VIDEO_HEVC;
                auto hvccBox = std::dynamic_pointer_cast<BoxHvcc>(stsdBox->getChild("avc1")->getChild("hvcC"));
                if (hvccBox) {
                    videoInfo_->sps = hvccBox->sps.front();
                    videoInfo_->pps = hvccBox->pps.front();
                    videoInfo_->vps = hvccBox->vps.front();
                    videoInfo_->naluHeaderLength = hvccBox->naluByteSize + 1;
                }
            } else {
                videoInfo_->mediaInfo = MEDIA_MIMETYPE_UNKNOWN;
            }
        }
    }
    isOpened_ = true;
    buffer_ = std::make_unique<Buffer>(rootBox_->info.size);
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

DemuxerResult Mp4Demuxer::parseData(DataPacket& packet) noexcept {
    if (packet.empty() || !isOpened_) {
        return {DemuxerResultCode::Failed, AVFramePtrArray(), AVFramePtrArray()};
    }
    if (!buffer_->append(static_cast<uint64_t>(packet.offset), std::move(packet.data))) {
        return {DemuxerResultCode::Normal, AVFramePtrArray(), AVFramePtrArray()};
    }
    DemuxerResult result;
    while(!buffer_->empty()) {
        uint64_t offset = INT64_MAX; //To find the earliest starting track
        std::shared_ptr<TrackContext> parseTrack;
        for (const auto& track : std::views::values(tracks_)) {
            uint64_t tOffset = INT64_MAX;
            if (!track->isInRange(*buffer_, tOffset)) {
                continue;
            } else if (tOffset < offset && parseTrack != track){
                parseTrack = track;
                offset = tOffset;
            }
        }
        if (!parseTrack) {
            break;
        }
        if (parseTrack->type == TrackType::Audio) {
            auto info = std::make_shared<AudioFrameInfo>();
            info->bitsPerSample = audioInfo_->bitsPerSample;
            info->channels = audioInfo_->channels;
            info->sampleRate = audioInfo_->sampleRate;
            parseTrack->parseData(*buffer_, info, result.audioFrames);
        } else if (parseTrack->type == TrackType::Video) {
            auto info = std::make_shared<VideoFrameInfo>();
            info->width = videoInfo_->width;
            info->height = videoInfo_->height;
            ///fix me: nalu size
            parseTrack->parseData(*buffer_, info, result.videoFrames);
        }
    }
    if (!result.audioFrames.empty() || !result.videoFrames.empty()) {
        buffer_->shrink();
    }
    auto mediaDataBox = rootBox_->getChild("mdat");
    if (mediaDataBox && buffer_->pos() >= (mediaDataBox->info.size + mediaDataBox->info.headerSize)) {
        isCompleted_ = true;
    }
    return result;
}

uint64_t Mp4Demuxer::getSeekToPos(long double time) noexcept {
    auto offset = headerInfo_->headerLength + 8;//skip size and type
    for (const auto& track:std::views::values(tracks_)) {
        if (track->type == TrackType::Video) {
            offset = track->getSeekPos(time);
        }
    }
    return offset;
}

void Mp4Demuxer::seekPos(uint64_t pos) noexcept  {
    IDemuxer::seekPos(pos);
    for (auto& track:std::views::values(tracks_)) {
        track->seek(pos);
    }
}

}

