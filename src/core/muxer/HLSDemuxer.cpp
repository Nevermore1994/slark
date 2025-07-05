//
//  HLSDemuxer.cpp
//  slark
//
//  Created by Nevermore on 2024/12/9.
//

#include "HLSDemuxer.h"
#include "Util.hpp"
#include "MediaUtil.h"
#include <format>
#include <utility>

namespace slark {

M3U8Parser::M3U8Parser(std::string  baseUrl)
    : baseUrl_(std::move(baseUrl)) {
    
}

void M3U8Parser::reset() noexcept {
    isPlayList_ = false;
    isCompleted_ = false;
    infos_.clear();
    playListInfos_.clear();
}

std::string spliceUrl(const std::string& baseUrl, std::string_view view) {
    if (view.starts_with("http")) {
        return std::string(view);
    }
    // Helper function to normalize "../"
    static auto normalizeRelativePath = [](std::string baseUrl, std::string_view& view) {
        if (baseUrl.back() == '/') {
            baseUrl.pop_back();
        }
        while (view.starts_with("../")) {
            view.remove_prefix(3);
            auto backslashPos = baseUrl.rfind('/');
            if (backslashPos != std::string::npos) {
                baseUrl = baseUrl.substr(0, backslashPos);
            } else {
                baseUrl.clear(); // Handle malformed paths
                break;
            }
        }
        return baseUrl;
    };
    
    // Handle "../" paths
    if (view.starts_with("../")) {
        auto url = normalizeRelativePath(baseUrl, view);
        return url + '/' + std::string(view);
    }
    
    // Handle root-relative paths "/path"
    if (view.starts_with('/')) {
        auto protocolEnd = baseUrl.find("//") + 2;
        auto hostEnd = baseUrl.find('/', protocolEnd);
        std::string baseHost = baseUrl.substr(0, hostEnd);
        return baseHost + std::string(view);
    }
    
    // Handle "./" paths
    while (view.starts_with("./")) {
        view.remove_prefix(2);
    }
    
    // Handle remaining cases (relative to baseUrl)
    return baseUrl + "/" + std::string(view);
}

bool M3U8Parser::parse(Buffer& buffer) noexcept {
    DataView line;
    while(buffer.readLine(line)) {
        auto content = line.removeSpace();
        if (content.empty()) {
            continue;
        }
        auto view = std::string_view(content);
        if(view.starts_with("#EXTINF:")) {
            view = view.substr(8);
            view = view.substr(0, view.find(','));
            TSInfo info;
            info.sequence = index++;
            info.duration = std::stod(std::string(view));
            info.startTime = totalDuration_;
            infos_.push_back(info);
            totalDuration_ += info.duration;
        } else if (view.starts_with("#EXT-X-BYTERANGE:")) {
            view = view.substr(17);
            if (infos_.empty()) {
                LogE("parse byte range error: info array is empty!");
                break;
            }
            auto& info = infos_.back();
            info.isSupportByteRange = true;
            auto pos = view.find('@');
            if (pos == std::string_view::npos) {
                LogE("parse byte range error");
                break;
            }
            auto lengthView = view.substr(0, pos);
            auto offsetView = view.substr(pos + 1);
            info.range.pos = static_cast<uint64_t>(std::stol(std::string(offsetView)));
            info.range.size = std::stol(std::string(lengthView));
        } else if (view.starts_with("#EXT-X-DISCONTINUITY")) {
            if (!infos_.empty()) {
                infos_.back().isDiscontinuity = true; //The next TS needs to reset the decoder
            }
        } else if (view.starts_with("#EXT-X-STREAM-INF:")) {
            //play list
            isPlayList_ = true;
            view = view.substr(18);
            view = view.substr(0, view.find(','));
            view = view.substr(view.find('=') + 1);
            PlayListInfo info;
            info.codeRate = static_cast<uint32_t>(std::stoi(std::string(view)));
            playListInfos_.push_back(info);
        } else if(view.starts_with("#EXT-X-ENDLIST")) {
            isCompleted_ = true;
            LogI("parse m3u8 is completed.");
        } else if (!view.starts_with("#")) {
            if (isPlayList_) {
                if (playListInfos_.empty()) {
                    LogE("parse play list: info array is empty!");
                    break;
                }
                playListInfos_.back().m3u8Url = spliceUrl(baseUrl_, view);
                auto pos = view.find('.');
                if (pos == std::string_view::npos) {
                    playListInfos_.back().name = view;
                } else {
                    playListInfos_.back().name = view.substr(0, pos);
                }
            } else {
                if (infos_.empty()) {
                    LogE("parse url: info array is empty!");
                    break;
                }
                infos_.back().url = spliceUrl(baseUrl_, view);
            }
        }
    }
    
    if (!line.empty()) {
        line = DataView(); // cannot be used anymore
        buffer.shrink();
    }
    auto view = buffer.view();
    if (view.startWith("#EXT-X-ENDLIST")) {
        isCompleted_ = true;
        LogI("parse m3u8 is completed.");
    }
    return isCompleted_;
}

bool TSDemuxer::checkPacket(Buffer& buffer, uint64_t& pos) noexcept {
    constexpr char kSyncByte = 0x47;
    auto view = buffer.shotView();
    auto p = view.find(kSyncByte);
    if (p == std::string_view::npos) {
        return false;
    }
    if (view.substr(p).size() >= kPacketSize) {
        pos = buffer.pos() + p;
        return true;
    }
    return false;
}

bool TSDemuxer::parsePacket(DataView data, uint32_t tsIndex, DemuxerResult& result) noexcept {
    TSHeader header; //4byte
    Util::readByte(data, header.syncbyte);
    header.payloadUinitIndicator = (static_cast<uint8_t>(data[1]) & 0x40) >> 6;
    header.errorIndicator = (static_cast<uint8_t>(data[1]) & 0x80) >> 7;
    header.pid = static_cast<uint16_t>(
        ((static_cast<uint8_t>(data[1]) & 0x1f) << 8) | (static_cast<uint8_t>(data[2]))
    );
    header.adaptationFieldControl = (static_cast<uint8_t>(data[3]) & 0x30) >> 4;
    data = data.substr(4);
    if (header.syncbyte != 0x47) {
        LogE("sync byte error:{}", header.syncbyte);
        return false;
    }
    if (header.errorIndicator) {
        LogE("transport error:{}", header.errorIndicator);
        return false;
    }
    
    //has adaption
    if ((header.adaptationFieldControl & 0x02) == 0x02) {
        uint8_t adaptionLen;
        Util::readByte(data, adaptionLen);
        data = data.substr(adaptionLen + 1);
    }
    
    if (header.payloadUinitIndicator &&
        (header.pid == 0 || header.pid == pat_.pmtPid)) {
        //skip psi
        data = data.substr(1);
    }
    
    if (header.pid == 0) {
        parseTSPAT(data);
    } else if (header.pid == pat_.pmtPid) {
        parseTSPMT(data);
    } else if (header.pid == pmt_.videoPid) {
        if (header.payloadUinitIndicator == 1 && videoInfo_) {
            if (videoInfo_->mediaInfo == MEDIA_MIMETYPE_VIDEO_AVC) {
                packH264VideoPacket(tsIndex, result.videoFrames);
            } else {
                //TODO: pack h265
            }
        }
        parseTSPes(data, header.payloadUinitIndicator, videoESFrame_);
    } else if (header.pid == pmt_.audioPid) {
        if (header.payloadUinitIndicator == 1) {
            packAudioPacket(tsIndex, result.audioFrames);
        }
        parseTSPes(data, header.payloadUinitIndicator, audioESFrame_);
    }
    return true;
}

bool TSDemuxer::parseTSPAT(DataView data) noexcept {
    if (data[0] != 0) {
        LogE("pat table id != 0, {}", data[0]);
        return false;
    }
    uint8_t syntaxIndicator = (static_cast<uint8_t>(data[1]) & 0x80) >> 7;
    if (syntaxIndicator != 1) {
        LogE("syntax indicator != 1, {}", syntaxIndicator);
        return false;
    }
    pat_.sectionLength = ( static_cast<uint8_t>(data[1]) & 0x0f << 8) |
                            static_cast<uint8_t>(data[2] );
    pat_.currentNextIndicator = static_cast<uint8_t>(data[5]) & 0x01;
    data = data.substr(8);
    if (pat_.currentNextIndicator == 1) {
        auto infoLength = pat_.sectionLength - 5 - 4; //streamId, sectionNumber + crc32
        while (infoLength >= 4) {
            auto pnum = static_cast<uint16_t>(
                (static_cast<uint8_t>(data[0]) << 8) | static_cast<uint8_t>(data[1])
            );
            auto pid = static_cast<uint16_t>(
                ((static_cast<uint8_t>(data[2]) & 0x1f) << 8) | static_cast<uint8_t>(data[3])
            );
            if (pnum == 0x0001) {
                pat_.pmtPid = pid;
            }
            infoLength -= 4;
            data = data.substr(4);
        }
    }
    return true;
}

bool TSDemuxer::parseTSPMT(DataView view) noexcept {
    uint8_t syntaxIndicator = (view[1] & 0x80) >> 7;
    if (syntaxIndicator != 1) {
        LogE("syntax indicator != 1, {}", syntaxIndicator);
        return false;
    }
    pmt_.sectionLength = (view[1] & 0x0f << 8) | view[2];
    pmt_.progamInfoLength = (view[10] & 0x0f << 8) | view[11];
    view = view.substr(12);
    //4:crc32, 9:program info length
    auto infoLength = pmt_.sectionLength - 4 - 9 - pmt_.progamInfoLength;
    while (infoLength >= 5) {
        uint8_t streamType = view[0];
        auto pid = static_cast<uint16_t>(((view[1] & 0x1f) << 8) | view[2]);
        auto esInfoLength = static_cast<uint16_t>(((view[3] & 0x0f) << 8) | view[4]);
        if (streamType == 0x1b || streamType == 0x24) {
            //h264
            pmt_.videoPid = pid;
            if (!videoInfo_) {
                videoInfo_ = std::make_shared<VideoInfo>();
                if (streamType == 0x1b) {
                    videoInfo_->mediaInfo = MEDIA_MIMETYPE_VIDEO_AVC;
                } else {
                    videoInfo_->mediaInfo = MEDIA_MIMETYPE_VIDEO_HEVC;
                }
            }

        } else if (streamType == 0x0f) {
            //aac
            pmt_.audioPid = pid;
            if (!audioInfo_) {
                audioInfo_ = std::make_shared<AudioInfo>();
                audioInfo_->mediaInfo = MEDIA_MIMETYPE_AUDIO_AAC;
            }
        }
        infoLength -= 5 + esInfoLength;
        view = view.substr(5 + esInfoLength);
    }
    return true;
}

bool TSDemuxer::parseTSPes(DataView data, uint8_t payloadIndicator, TSPESFrame& pes) noexcept {
    if (payloadIndicator != 1) {
        pes.mediaData.append(data);
        return true;
    }
    
    if (data.length() < 6) {
        LogE("data length is too short.");
        return false;
    }
    //pes header: start code 0x00 0x00 0x01, stream id (1 byte), packet length(2 byte)
    if (data[0] != 0 || data[1] != 0 || data[2] != 1) {
        LogE("start code error:{} {} {}", data[0], data[1], data[2]);
        return false;
    }
    
    auto isAudio = (&pes == &audioESFrame_);
    uint8_t streamId = data[3];
    LogI("{} stream id:{}, pesPacketLength:{}", isAudio ? "audio" : "video",
         data[3], (data[4] << 8) | data[5]);
    
    static const std::array<uint8_t, 6> kSpecialStreamArray = {0xbc, 0xbe, 0xbf, 0xf0, 0xf1, 0xff};
    auto isSpecialStream = std::any_of(kSpecialStreamArray.begin(), kSpecialStreamArray.end(), [pesStreamId = streamId](uint8_t streamId) {
        return pesStreamId == streamId;
    });
    uint32_t dataOffset = 0;
    if (!isSpecialStream) {
        if (data.length() < 20) {
            LogE("data length is too short");
            return false;
        }
        uint8_t ptsFlag = (data[7] & 0xc0) >> 6;
        LogI("pes flag info:{}", (data[7] & 0x3f));
        auto headerDataLength = data[8];
        if (ptsFlag & 0x02) {
            if (headerDataLength >= 5) {
                //PTS = PTS[32..30] + PTS[29..15] + PTS[14..0]
                pes.pts = static_cast<int64_t>(((data[9] & 0x0e) << 29) | ((data[10] & 0xff) << 22) |
                                                ((data[11] & 0xfe) << 14) | ((data[12] & 0xff) << 7) |
                                                ((data[13] & 0xfe) >> 1));
            } else {
                LogE("pes header data length error:{}", headerDataLength);
            }
        }
        if (ptsFlag & 0x01) {
            if (headerDataLength >= 10) {
                //DTS = DTS[32..30] + PTS[29..15] + PTS[14..0]
                pes.dts = static_cast<int64_t>(((data[14] & 0x0e) << 29) | ((data[15] & 0xff) << 22) |
                                                ((data[16] & 0xfe) << 14) | ((data[17] & 0xff) << 7) |
                                                ((data[18] & 0xfe) >> 1));
            } else {
                LogE("pes header data length error:{}", headerDataLength);
            }
        } else {
            pes.dts = pes.pts;
        }
        //pes header (6 byte), flags (3 byte)
        dataOffset = 6 + 3 + headerDataLength;
    } else if (streamId != 0xbe) {
        dataOffset = 6; //pes header (6 byte)
    }
    SAssert(dataOffset < data.length(), "error! data offset > data length!");
    pes.mediaData.append(data.substr(dataOffset));
    return true;
}

void TSDemuxer::writeVideoData(AVFramePtr& frame, DataView view) noexcept {
    Data header(4);
#if SLARK_ANDROID
    header.rawData[0] = 0x0;
    header.rawData[1] = 0x0;
    header.rawData[2] = 0x0;
    header.rawData[3] = 0x1;
#elif SLARK_IOS
    auto length = static_cast<uint32_t>(view.length());
    header.rawData[0] = static_cast<uint8_t>(length >> 24);
    header.rawData[1] = static_cast<uint8_t>(length >> 16);
    header.rawData[2] = static_cast<uint8_t>(length >> 8);
    header.rawData[3] = static_cast<uint8_t>(length);
#endif
    header.length = 4;
    frame->data->append(header); //write header
    frame->data->append(view);
}

bool TSDemuxer::packH264VideoPacket(uint32_t tsIndex, AVFramePtrArray& frames) noexcept {
    if (videoESFrame_.mediaData.empty()) {
        LogI("media data is empty!");
        return false;
    }
    if (!parseInfo_.videoInfo.isValid) {
        parseInfo_.videoInfo.firstDts = videoESFrame_.pts;
        parseInfo_.videoInfo.firstPts = videoESFrame_.dts;
        parseInfo_.videoInfo.isValid = true;
    }
    std::vector<Range> naluRanges;
    auto view = videoESFrame_.mediaData.view();
    uint64_t pos = 0;
    while(!view.empty()) {
        Range range;
        if (!findNaluUnit(view, range)) {
            break;
        }
        view = view.substr(range.end());
        range.shift(static_cast<int64_t>(pos));
        pos = range.end();
        naluRanges.push_back(range);
    }
    
    view = videoESFrame_.mediaData.view();
    for (const auto& range : naluRanges) {
        auto dataView = view.substr(range);
        auto naluType = static_cast<uint8_t>(dataView[0]) & 0x1f;
        auto info = std::make_shared<VideoFrameInfo>();
        if (naluType == 7) {
            if (!videoInfo_->sps || videoInfo_->sps->view() != dataView) {
                parseH264Sps(dataView, videoInfo_);
                videoInfo_->naluHeaderLength = 4;
                videoInfo_->sps = std::make_shared<Data>();
                videoInfo_->sps->append(dataView);
                parseInfo_.isNotifiedHeader = false;
            }
            continue;
        } else if (naluType == 8) {
            if (!videoInfo_->pps || videoInfo_->pps->view() != dataView) {
                videoInfo_->pps = std::make_shared<Data>();
                videoInfo_->pps->append(dataView);
                parseInfo_.isNotifiedHeader = false;
            }
            continue;
        } else if (naluType == 5 || naluType == 1) {
            auto [firstMBInSlice, sliceType] = parseSliceType(dataView);
            if (firstMBInSlice != 0) {
                //multi slice
                if (frames.empty()) {
                    LogE("frames array is empty!");
                } else {
                    writeVideoData(frames.back(), dataView);
                }
                continue;
            }
            if (naluType == 5) {
                info->isIDRFrame = true;
                info->frameType = VideoFrameType::IFrame;
            } else if (sliceType == 0 || sliceType == 5) {
                info->frameType = VideoFrameType::PFrame;
            } else if (sliceType == 1 || sliceType == 6) {
                info->frameType = VideoFrameType::BFrame;
            } else if (sliceType == 2 || sliceType == 7) {
                info->frameType = VideoFrameType::IFrame;
            }
        } else {
            continue;
        }
        
        auto frame = std::make_unique<AVFrame>(AVFrameType::Video);
        frame->info = info;
        frame->data = std::make_unique<Data>();
        writeVideoData(frame, dataView);
        frame->timeScale = 90000; //90k hz
        frame->pts = videoESFrame_.pts;
        frame->dts = videoESFrame_.dts;
        frame->index = parseInfo_.videoInfo.frameIndex++;
        info->width = videoInfo_->width;
        info->height = videoInfo_->height;
        recalculatePtsDts(frame->pts, frame->dts, false);
        frame->pts -= parseInfo_.videoInfo.firstPts;
        frame->dts -= parseInfo_.videoInfo.firstDts;
    
        if (parseInfo_.calculatedFps == 0) {
            if (frame->index != 0) {
                frame->duration = static_cast<uint32_t>( (frame->ptsTime() /
                    static_cast<double>(frame->index)) * 1000);
                parseInfo_.calculatedFps = static_cast<uint32_t>(round(1000.0 / static_cast<double>(frame->duration)));
            } else {
                frame->duration = 33; //default 33 ms
            }
        } else {
            frame->duration = 1000 / parseInfo_.calculatedFps;
        }
        frames.push_back(std::move(frame));
    }
    videoESFrame_.reset();
    return true;
}

void TSDemuxer::recalculatePtsDts(int64_t& pts, int64_t& dts, bool isAudio) noexcept {
    //Offset to handle wraparound
    auto& fixInfo = isAudio ? audioFixInfo_: videoFixInfo_;
    if (dts < fixInfo.preDtsTime) {
        fixInfo.offset += TSPtsFixInfo::kMaxPTS;
        LogI("dts wraparound:{}, {}, offset:{}", dts, fixInfo.preDtsTime, fixInfo.offset);
    }
    fixInfo.preDtsTime = dts;
    dts += fixInfo.offset;
    pts += fixInfo.offset;
    LogI("[seek info] {} parse data:{},", isAudio ? "audio" : "video" , fixInfo.preDtsTime);
}

bool findAdtsHeader(DataView view, uint32_t& pos) noexcept {
    pos = 0;
    while (view.length() >= 7) {
        if (view[0] == 0xff && (view[1] & 0xf0) == 0xf0) {
            return true;
        }
        pos++;
        view = view.substr(pos);
    }
    return false;
}

bool parseAdtsHeader(DataView dataView, AACAdtsHeader& header) noexcept {
    if (dataView.length() < 7) {
        LogE("parse header, data length is insufficient!");
        return false;
    }
    auto syncword = static_cast<uint16_t>((dataView[0] << 4) | (dataView[1] >> 4));
    if (syncword != 0xFFF) {
        LogE("sync word error!");
        return false;
    }
    header.id = (dataView[1] >> 3) & 0x1;
    header.layer = (dataView[1] >> 1) & 0x3;
    header.hasCRC = !(dataView[1] & 0x1); //1 no CRC data
    header.profile = (dataView[2] >> 6) & 0x3;
    header.samplingIndex = (dataView[2] >> 2) & 0xF;
    header.channel = static_cast<uint8_t>(((dataView[2] & 0x1) << 2) | ((dataView[3] >> 6) & 0x3));
    header.frameLength = static_cast<uint16_t>(((dataView[3] & 0x3) << 11) | (dataView[4] << 3) | (dataView[5] >> 5));
    header.bufferFullness = static_cast<uint16_t>(((dataView[5] & 0x1F) << 6) | ((dataView[6] >> 2) & 0x3f));
    header.blockCount = dataView[6] & 0x3;
    return true;
}

void TSDemuxer::resetData() noexcept {
    audioFixInfo_.reset();
    videoFixInfo_.reset();
    videoESFrame_.reset();
    audioESFrame_.reset();
    LogI("[seek info] reset data");
}

bool TSDemuxer::packAudioPacket(uint32_t tsIndex, AVFramePtrArray& frames) noexcept {
    if (audioESFrame_.mediaData.empty()) {
        LogI("media data is empty!");
        return false;
    }
    auto view = audioESFrame_.mediaData.view();
    uint32_t start = 0;
    if (!findAdtsHeader(view, start)) {
        return false;
    }
    view = view.substr(start);
    while (view.length() > 7) {
        AACAdtsHeader header;
        if (!parseAdtsHeader(view, header)) {
            LogE("parse aac adts header.");
            break;
        }
        auto headerLength = header.hasCRC ? 9 : 7;
        if (header.frameLength <= headerLength) {
            LogE("frame length:{}, header length:{}", header.frameLength, headerLength);
            break;
        }
        if (header.frameLength > view.length()) {
            LogE("frame length:{}, view length:{}", header.frameLength, view.length());
            return false;
        }
        if (header.channel == 0) {
            header.channel = 2;
        }
        auto dataView = view.substr(0, header.frameLength);
        dataView = dataView.substr(static_cast<uint64_t>(headerLength));
        auto info = std::make_shared<AudioFrameInfo>();
        info->channels = header.channel;
        info->sampleRate = static_cast<uint64_t>(getAACSamplingRate(header.samplingIndex));
        info->refIndex = tsIndex;
        auto frame = std::make_unique<AVFrame>(AVFrameType::Audio);
        frame->data = std::make_unique<Data>(dataView);
        frame->duration = 1024 * 1000 / info->sampleRate; //ms
        frame->info = info;
        frame->timeScale = 90000;//90k
        frame->pts = audioESFrame_.pts;
        frame->dts = audioESFrame_.dts;
        frame->index = parseInfo_.audioInfo.frameIndex++;
        recalculatePtsDts(frame->pts, frame->dts, true);
        
        if (audioInfo_ && !parseInfo_.audioInfo.isValid) {
            audioInfo_->channels = header.channel;
            audioInfo_->profile = getAudioProfile(header.profile + 1); //the adts profile starts at 0
            audioInfo_->sampleRate = info->sampleRate;
            audioInfo_->timeScale = 90000;
            audioInfo_->bitsPerSample = 16; //default uint16
            audioInfo_->samplingFrequencyIndex = header.samplingIndex;
            parseInfo_.audioInfo.firstPts = frame->pts;
            parseInfo_.audioInfo.firstDts = frame->dts;
            parseInfo_.audioInfo.isValid = true;
        }
        
        frame->pts -= parseInfo_.audioInfo.firstPts;
        frame->dts -= parseInfo_.audioInfo.firstDts;
        frames.push_back(std::move(frame));
        view = view.substr(header.frameLength);
    }
    audioESFrame_.reset();
    return true;
}

bool TSDemuxer::parseData(Buffer& buffer, uint32_t tsIndex, DemuxerResult& result) noexcept {
    if (!buffer.require(kPacketSize)) {
        return false;
    }
    uint64_t pos = std::string_view::npos;
    auto isNotifiedHeader = parseInfo_.isNotifiedHeader;
    while (checkPacket(buffer, pos)) {
        buffer.skipTo(static_cast<int64_t>(pos));
        auto view = buffer.shotView().substr(0, kPacketSize);
        parsePacket(view, tsIndex, result);
        buffer.skipTo(static_cast<int64_t>(pos + kPacketSize));
    }
    if (!result.videoFrames.empty() || !result.audioFrames.empty()) {
        buffer.shrink();
    }
    if (!parseInfo_.isNotifiedHeader && parseInfo_.isReady()) {
        if (videoInfo_->fps == 0) {
            videoInfo_->fps = static_cast<uint16_t>(parseInfo_.calculatedFps);
        }
        parseInfo_.isNotifiedHeader = true;
        result.resultCode = isNotifiedHeader ? DemuxerResultCode::VideoInfoChange :  DemuxerResultCode::ParsedHeader;
    } else if (videoInfo_->fps == 0 && parseInfo_.isNotifiedHeader) {
        videoInfo_->fps = static_cast<uint16_t>(parseInfo_.calculatedFps);
        result.resultCode = DemuxerResultCode::ParsedFPS;
    }
    return true;
}

DemuxerResult HLSDemuxer::parseData(DataPacket& packet) noexcept {
    DemuxerResult result;
    if (packet.empty() || !isOpened_) {
        result.resultCode = DemuxerResultCode::Failed;
        return result;
    }
    uint64_t tsIndex = 0;
    if (!packet.tag.empty()) {
        tsIndex = static_cast<uint64_t>(std::stoi(packet.tag));
    } else {
        LogI("data packet tag is empty!");
    }
    
    if (seekTsIndex_ != kInvalidTSIndex && seekTsIndex_ != tsIndex) {
        LogI("[seek info], seek index:{} expired data:{},", seekTsIndex_, tsIndex);
        return result;
    } else {
        seekTsIndex_ = kInvalidTSIndex; //reset
    }
    
    if (!buffer_->append(static_cast<uint64_t>(packet.offset), std::move(packet.data))) {
        LogE("discard data, ts index:{}, data offset:{}, parse offset:{}",
             tsIndex, packet.offset, buffer_->offset());
        return result;
    }
    
    if (!tsDemuxer_->parseData(*buffer_, static_cast<uint32_t>(tsIndex), result)) {
        LogE("parse ts data error! offset:{}, ts index:{}", packet.offset, tsIndex);
        result.resultCode = DemuxerResultCode::Failed;
    }
    return result;
}

void HLSDemuxer::init(DemuxerConfig config) noexcept {
    IDemuxer::init(std::move(config));
    auto pos = config_.filePath.find_last_of('/');
    if (pos != std::string::npos) {
        baseUrl_ = config_.filePath.substr(0, pos);
    } else {
        baseUrl_ = config_.filePath;
    }
    mainParser_ = std::make_unique<M3U8Parser>(baseUrl_);
    buffer_ = std::make_unique<Buffer>();
    tsDemuxer_ = std::make_unique<TSDemuxer>(audioInfo_, videoInfo_);
}

bool HLSDemuxer::open(std::unique_ptr<Buffer>& buffer) noexcept {
    if (!mainParser_->isCompleted()) {
        if (!mainParser_->parse(*buffer)) {
            LogI("need more data.");
            return false;
        }
        LogI("parser hls info complete:{}", mainParser_->isPlayList() ? "play list" : "normal");
        if (mainParser_->isPlayList()) {
            slinkParser_ = std::make_unique<M3U8Parser>(baseUrl_);
        } else {
            totalDuration_ = CTime(mainParser_->totalDuration());
            isOpened_ = true;
        }
    } else {
        if (!slinkParser_->parse(*buffer)) {
            LogI("need more data.");
            return false;
        }
        LogI("parser hls info complete");
        totalDuration_ = CTime(slinkParser_->totalDuration());
        isOpened_ = true;
    }
    return true;
}

void HLSDemuxer::close() noexcept {
    seekTsIndex_ = kInvalidTSIndex;
    baseUrl_.clear();
    buffer_.reset();
    mainParser_.reset();
    slinkParser_.reset();
    tsDemuxer_.reset();
}

void HLSDemuxer::seekPos(uint64_t index) noexcept {
    if (!isOpened_) {
        LogE("demuxer closed");
        return;
    }
    auto& infos = getTSInfos();
    auto tsIndex = static_cast<size_t>(index);
    if (tsIndex < infos.size()) {
        auto pos = infos[tsIndex].range.start();
        IDemuxer::seekPos(pos);
    }
    if (tsDemuxer_) {
        tsDemuxer_->resetData();
        buffer_->reset();
    }
    seekTsIndex_ = tsIndex;
    LogI("[seek info] seekTsIndex:{}", seekTsIndex_);
}

uint64_t HLSDemuxer::getSeekToPos(double time) noexcept {
    if (!isOpened_) {
        return kInvalidTSIndex;
    }
    auto comp = [time](const auto& tsInfo){
        return tsInfo.startTime <= time && time <= tsInfo.endTime();
    };
    auto& infos = getTSInfos();
    auto it = std::find_if(infos.begin(), infos.end(), comp);
    if (it == infos.end()) {
        return kInvalidTSIndex;
    }
    return it->sequence;
}

const std::vector<TSInfo>& HLSDemuxer::getTSInfos() const noexcept {
    SAssert(mainParser_ != nullptr, "demuxer closed");
    if (isPlayList()) {
        return slinkParser_->TSInfos();
    }
    return mainParser_->TSInfos();
}

}

