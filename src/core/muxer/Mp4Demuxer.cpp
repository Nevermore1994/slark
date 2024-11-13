//
//  Mp4Demuxer.cpp
//  slark
//
//  Created by Nevermore
//

#include <stack>
#include <ranges>
#include "Mp4Demuxer.hpp"
#include "Log.hpp"
#include "Util.hpp"
#include "MediaDefs.h"

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

void TrackContext::seek(uint64_t pos)  noexcept {
    if (!stco) {
        return;
    }
    const auto& samples = stco->chunkOffsets;
    auto it = std::lower_bound(samples.begin(), samples.end(), pos);
    if (it == samples.end()) {
        return;
    }
    index = std::distance(samples.begin(), it);
    reset();
    for(size_t i = 0; i < index; i++) {
        calcIndex();
    }
}

void TrackContext::reset() noexcept {
    sampleSizeIndex = 0;
    entryIndex = 0;
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
}

bool TrackContext::calcIndex() noexcept {
    auto size = stsz->sampleSizes[sampleSizeIndex];
    auto& entry = stsc->entrys[entryIndex];
    auto& sttsEntry = stts->entrys[sttsEntryIndex];
    dts += sttsEntry.sampleDelta;
    if (ctts) {
        pts = dts + ctts->entrys[cttsEntryIndex].sampleOffset;
    } else {
        pts = dts;
    }
    
    sampleSizeIndex++;
    
    sampleOffset += size;
    entrySampleIndex++;
    if (entrySampleIndex >= entry.samplesPerChunk) {
        if (chunkLogicIndex + 1 < stco->chunkOffsets.size()) {
            chunkLogicIndex++;
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
    
    if (entryIndex + 1 < stsc->entrys.size()) {
        if (chunkLogicIndex >= (stsc->entrys[entryIndex + 1].firstChunk - 1)) {
            entryIndex++;
        }
    }
}

bool TrackContext::isInRange(Buffer& buffer) noexcept {
    if (sampleSizeIndex >= stsz->sampleSizes.size() || entryIndex >= stsc->entrys.size()) {
        return false;
    }
    auto start = stco->chunkOffsets[chunkLogicIndex] + static_cast<uint64_t>(sampleOffset);
    auto end = start + stsz->sampleSizes[sampleSizeIndex];
    auto bufferStart = buffer.offset();
    auto bufferEnd = bufferStart + buffer.length();
    if (bufferStart <= start && end <= bufferEnd) {
        return true;
    }
    return false;
}

bool TrackContext::isInRange(Buffer& buffer, uint64_t& offset) noexcept {
    if (sampleSizeIndex >= stsz->sampleSizes.size() || entryIndex >= stsc->entrys.size()) {
        return false;
    }
    auto start = stco->chunkOffsets[chunkLogicIndex] + static_cast<uint64_t>(sampleOffset);
    auto end = start + stsz->sampleSizes[sampleSizeIndex];
    auto bufferStart = buffer.offset();
    auto bufferEnd = bufferStart + buffer.length();
    if (bufferStart <= start && end <= bufferEnd) {
        offset = start;
        return true;
    }
    return false;
}

AVFrameArray TrackContext::praseH264FrameData(AVFramePtr frame, DataPtr data, const std::any& frameInfo) {
    if (naluByteSize == 0) {
        auto avccBox = std::dynamic_pointer_cast<BoxAvcc>(stsd->getChild("avc1")->getChild("avcC"));
        if (avccBox) {
            naluByteSize = avccBox->naluByteSize;
        }
    }
    AVFrameArray frames;
    auto view = data->view();
    auto info = std::any_cast<VideoFrameInfo>(frameInfo);
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
        if (naluType == 5) {
            info.isKeyFrame = true;
            keyIndex = frame->index;
            if (totalSize == data->length) {
                frame->data = std::move(data);
                view = {};
            } else {
                frame->data = std::make_unique<Data>(view.substr(0, totalSize));
                view = view.substr(totalSize);
            }
        } else if (naluType == 1) {
            info.isKeyFrame = false;
            info.keyIndex = keyIndex;
            if (naluSize == data->length) {
                frame->data = std::move(data);
                view = {};
            } else {
                frame->data = std::make_unique<Data>(view.substr(0, totalSize));
                view = view.substr(totalSize);
            }
        } else if (naluType == 2) {
            //slice
        } else if (naluType == 6) {
            //sei
            auto seiFrame = frame->copy();
            seiFrame->info = info;
            seiFrame->data = std::make_unique<Data>(view.substr(0, totalSize));
            view = view.substr(totalSize);
            frames.push_back(std::move(seiFrame));
        }
    }
    info.keyIndex = keyIndex;
    frame->info = info;
    frames.push_back(std::move(frame));
    return frames;
}

void TrackContext::parseData(Buffer& buffer, const std::any& frameInfo, AVFrameArray& packets)  {
    if (!isInRange(buffer)) {
        return;
    }
    auto start = stco->chunkOffsets[chunkLogicIndex] + static_cast<uint64_t>(sampleOffset);
    auto size =  stsz->sampleSizes[sampleSizeIndex];
    if (!buffer.skipTo(start)) {
        return;
    }
    auto frame = std::make_unique<AVFrame>();
    frame->index = ++index;
    frame->dts = dts;
    frame->pts = pts;
    frame->offset = start;
    frame->timeScale = mdhd->timeScale;
    if (type == TrackType::Audio) {
        frame->frameType = AVFrameType::Audio;
        auto info = std::any_cast<AudioFrameInfo>(frameInfo);
        frame->duration = static_cast<uint32_t>(info.duration(size) * 1000.0);
        frame->info = frameInfo;
        frame->data = buffer.readData(size);
        packets.push_back(std::move(frame));
    } else if (type == TrackType::Video) {
        frame->frameType = AVFrameType::Video;
        auto data = buffer.readData(size);
        if (codecId == CodecId::AVC) {
            auto frames = praseH264FrameData(std::move(frame), std::move(data), frameInfo);
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
    start = boxStart + buffer.offset(); //size start pos
    if (start < 0) {
        return false;
    }
    auto sizeView = view.substr(boxStart, 4);
    return Util::read4ByteBE(sizeView, size);
}

bool Mp4Demuxer::parseMoovBox(Buffer& buffer, BoxRefPtr moovBox) noexcept {
    std::stack<BoxRefPtr> parentBox;
    parentBox.push(moovBox);
    auto size = moovBox->info.size - moovBox->info.headerSize;
    if (!buffer.require(size)) {
        return false;
    }
    bool res = true;
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
            auto skipOffset = box->info.start + box->info.size - buffer.pos();
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
    return true;
}

bool Mp4Demuxer::open(std::unique_ptr<Buffer>& buffer) noexcept {
    if (rootBox_ == nullptr) {
        BoxInfo info;
        info.start = 0;
        info.size = buffer->totalSize();
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
        auto skipOffset = box->info.start + box->info.size - buffer->pos();
        if (!buffer->skip(skipOffset)) {
            if (box->info.symbol != "mdat") {
                res = false;
            }
            break;
        }
    }
    
    if (!res) {
        buffer->resetReadPos();
    } else {
        init();
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

void Mp4Demuxer::init() noexcept {
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
                videoInfo_->fps = videoInfo_->timeScale / delta;
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
                    videoInfo_->naluHeaderLength = avccBox->naluByteSize;
                }
            } else if (codecId == CodecId::HEVC) {
                videoInfo_->mediaInfo = MEDIA_MIMETYPE_VIDEO_HEVC;
            } else {
                videoInfo_->mediaInfo = MEDIA_MIMETYPE_UNKNOWN;
            }
        }
    }
    isInited_ = true;
    buffer_ = std::make_unique<Buffer>(rootBox_->info.size);
}

void Mp4Demuxer::close() noexcept {
    rootBox_.reset();
    tracks_.clear();
    buffer_.reset();
    reset();
}

void recursiveDescription(BoxRefPtr ptr, const std::string& prefix, std::ostringstream& ss) {
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

DemuxerResult Mp4Demuxer::parseData(std::unique_ptr<Data> data, int64_t offset) noexcept {
    if (!data || data->empty() || !isInited_) {
        return {DemuxerResultCode::Failed, AVFrameArray(), AVFrameArray()};
    }
    auto length = data->length;
    if (!buffer_->append(static_cast<uint64_t>(offset), std::move(data))) {
        return {DemuxerResultCode::Normal, AVFrameArray(), AVFrameArray()};
    }
    receivedLength_ += length;
    DemuxerResult result;
    while(!buffer_->empty()) {
        uint64_t offset = INT64_MAX;
        std::shared_ptr<TrackContext> parseTrack;
        for (auto track : std::views::values(tracks_)) {
            uint64_t tOffset = INT64_MAX;
            if (!track->isInRange(*buffer_, tOffset)) {
                continue;
            } else if (tOffset < offset){
                parseTrack = track;
                offset = tOffset;
            }
        }
        if (!parseTrack) {
            break;
        }
        if (parseTrack->type == TrackType::Audio) {
            AudioFrameInfo info;
            info.bitsPerSample = audioInfo_->bitsPerSample;
            info.channels = audioInfo_->channels;
            info.sampleRate = audioInfo_->sampleRate;
            parseTrack->parseData(*buffer_, info, result.audioFrames);
        } else if (parseTrack->type == TrackType::Video) {
            VideoFrameInfo info;
            info.width = videoInfo_->width;
            info.height = videoInfo_->height;
            ///fix me: nalu size
            parseTrack->parseData(*buffer_, info, result.videoFrames);
        }
    }
    if (!result.audioFrames.empty() || !result.videoFrames.empty()) {
        buffer_->shrink();
    }
    return result;
}

uint64_t Mp4Demuxer::getSeekToPos(Time::TimePoint timePoint) noexcept {
    if (timePoint == 0) {
        auto startPos = headerInfo_->headerLength + 8;//skip size and type
        for (auto& track:std::views::values(tracks_)) {
            track->seek(startPos);
        }
        return startPos;
    }
    return 0;
}

}

