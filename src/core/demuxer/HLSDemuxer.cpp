//
//  HLSDemuxer.cpp
//  slark
//
//  Created by Nevermore on 2024/12/9.
//

#include "HLSDemuxer.h"

#include "DataView.hpp"
#include "HLSDemuxer.h"
#include "MediaUtil.h"
#include "Util.h"

#include <utility>

namespace slark {

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
    TSHeader header; // 4byte
    data.readNumberLE(header.syncbyte, 1);
    header.payloadUinitIndicator = (static_cast<uint8_t>(data[1]) & 0x40) >> 6;
    header.errorIndicator = (static_cast<uint8_t>(data[1]) & 0x80) >> 7;
    header.pid = static_cast<uint16_t>(((static_cast<uint8_t>(data[1]) & 0x1f) << 8) |
                                       (static_cast<uint8_t>(data[2])));
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

    // has adaption
    if ((header.adaptationFieldControl & 0x02) == 0x02) {
        uint8_t adaptionLen;
        data.readNumberLE(adaptionLen, 1);
        if (adaptionLen + 1 > data.length()) {
            LogE("adaption field overflow: len={}, remaining={}", adaptionLen, data.length());
            return false;
        }
        data = data.substr(adaptionLen + 1);
    }

    if (header.payloadUinitIndicator && (header.pid == 0 || header.pid == pat_.pmtPid)) {
        // skip psi
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
                // TODO: pack h265
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
    pat_.sectionLength = static_cast<uint16_t>(((static_cast<uint8_t>(data[1]) & 0x0f) << 8) |
                                               static_cast<uint8_t>(data[2]));
    pat_.currentNextIndicator = static_cast<uint8_t>(data[5]) & 0x01;
    data = data.substr(8);
    if (pat_.currentNextIndicator == 1) {
        // sectionLength 从第3字节起算，减去 transport_stream_id(2) + version/section(2) +
        // last_section(1) + crc32(4)
        constexpr int32_t kPatFixedOverhead = 5 + 4;
        if (pat_.sectionLength < kPatFixedOverhead) {
            LogE("pat section length too small: {}", pat_.sectionLength);
            return false;
        }
        auto infoLength = static_cast<int32_t>(pat_.sectionLength) - kPatFixedOverhead;
        while (infoLength >= 4) {
            auto pnum = static_cast<uint16_t>((static_cast<uint8_t>(data[0]) << 8) |
                                              static_cast<uint8_t>(data[1]));
            auto pid = static_cast<uint16_t>(((static_cast<uint8_t>(data[2]) & 0x1f) << 8) |
                                             static_cast<uint8_t>(data[3]));
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
    pmt_.sectionLength = static_cast<uint16_t>(((view[1] & 0x0f) << 8) | view[2]);
    pmt_.progamInfoLength = static_cast<uint16_t>(((view[10] & 0x0f) << 8) | view[11]);
    view = view.substr(12);
    // sectionLength 从第3字节起算，减去固定字段(9字节) + program_info + crc32(4字节)
    constexpr int32_t kPmtFixedOverhead = 9 + 4;
    if (pmt_.sectionLength < kPmtFixedOverhead + pmt_.progamInfoLength) {
        LogE("pmt section length too small: {}", pmt_.sectionLength);
        return false;
    }
    auto infoLength = static_cast<int32_t>(pmt_.sectionLength) - kPmtFixedOverhead -
                      static_cast<int32_t>(pmt_.progamInfoLength);
    while (infoLength >= 5) {
        uint8_t streamType = view[0];
        auto pid = static_cast<uint16_t>(((view[1] & 0x1f) << 8) | view[2]);
        auto esInfoLength = static_cast<uint16_t>(((view[3] & 0x0f) << 8) | view[4]);
        updateStreamInfo(streamType, pid);
        infoLength -= 5 + esInfoLength;
        view = view.substr(5 + esInfoLength);
    }
    return true;
}

void TSDemuxer::updateStreamInfo(uint8_t streamType, uint16_t pid) noexcept {
    if (streamType == 0x1b || streamType == 0x24) {
        pmt_.videoPid = pid;
        if (!videoInfo_) {
            videoInfo_ = std::make_shared<VideoInfo>();
            videoInfo_->mediaInfo =
                (streamType == 0x1b) ? MEDIA_MIMETYPE_VIDEO_AVC : MEDIA_MIMETYPE_VIDEO_HEVC;
        }
    } else if (streamType == 0x0f) {
        pmt_.audioPid = pid;
        if (!audioInfo_) {
            audioInfo_ = std::make_shared<AudioInfo>();
            audioInfo_->mediaInfo = MEDIA_MIMETYPE_AUDIO_AAC;
        }
    }
}

static int64_t parsePtsValue(DataView data, uint32_t offset) noexcept {
    return static_cast<int64_t>(((data[offset] & 0x0e) << 28) | ((data[offset + 1] & 0xff) << 20) |
                                ((data[offset + 2] & 0xfe) << 12) |
                                ((data[offset + 3] & 0xff) << 5) |
                                ((data[offset + 4] & 0xfe) >> 1));
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
    // pes header: start code 0x00 0x00 0x01, stream id (1 byte), packet length(2 byte)
    if (data[0] != 0 || data[1] != 0 || data[2] != 1) {
        LogE("start code error:{} {} {}", data[0], data[1], data[2]);
        return false;
    }

    auto isAudio = (&pes == &audioESFrame_);
    uint8_t streamId = data[3];
    LogI("{} stream id:{}, pesPacketLength:{}",
         isAudio ? "audio" : "video",
         streamId,
         (data[4] << 8) | data[5]);

    static const std::array<uint8_t, 6> kSpecialStreamArray = {0xbc, 0xbe, 0xbf, 0xf0, 0xf1, 0xff};
    const bool kIsSpecialStream =
        std::any_of(kSpecialStreamArray.begin(), kSpecialStreamArray.end(), [streamId](uint8_t s) {
            return s == streamId;
        });
    uint32_t dataOffset = 0;
    if (!kIsSpecialStream) {
        if (data.length() < 20) {
            LogE("data length is too short");
            return false;
        }
        uint8_t ptsFlag = (data[7] & 0xc0) >> 6;
        LogI("pes flag info:{}", (data[7] & 0x3f));
        auto headerDataLength = static_cast<uint8_t>(data[8]);
        // PTS: bit[32..30] + bit[29..15] + bit[14..0]
        if ((ptsFlag & 0x02) && headerDataLength >= 5) {
            pes.pts = parsePtsValue(data, 9);
        } else if (ptsFlag & 0x02) {
            LogE("pes header data length error:{}", headerDataLength);
        }
        // DTS: same layout starting at offset 14
        if ((ptsFlag & 0x01) && headerDataLength >= 10) {
            pes.dts = parsePtsValue(data, 14);
        } else if (ptsFlag & 0x01) {
            LogE("pes header data length error:{}", headerDataLength);
        } else {
            pes.dts = pes.pts;
        }
        dataOffset = 6 + 3 + headerDataLength; // pes header(6) + flags(3) + header data
    } else if (streamId != 0xbe) {
        dataOffset = 6;
    }
    if (dataOffset >= data.length()) {
        LogE("parseTSPes: dataOffset:{} >= data.length:{}", dataOffset, data.length());
        return false;
    }
    pes.mediaData.append(data.substr(dataOffset));
    return true;
}

void TSDemuxer::writeVideoData(AVFramePtr& frame, DataView view) noexcept {
    std::array<uint8_t, 4> headerBytes{};
#if SLARK_ANDROID
    headerBytes = {0x00, 0x00, 0x00, 0x01};
#elif SLARK_IOS
    auto len = static_cast<uint32_t>(view.length());
    headerBytes = {static_cast<uint8_t>(len >> 24),
                   static_cast<uint8_t>(len >> 16),
                   static_cast<uint8_t>(len >> 8),
                   static_cast<uint8_t>(len)};
#endif
    frame->data->append(ConstByteSpan(headerBytes.data(), headerBytes.size()));
    frame->data->append(view);
}

std::vector<Range> TSDemuxer::collectNaluRanges(DataView view) noexcept {
    std::vector<Range> naluRanges;
    uint64_t globalPos = 0;
    while (!view.empty()) {
        Range range;
        if (!findNaluUnit(view, range)) {
            break;
        }
        // range.pos 是 nalu 数据起始，range.size 是 nalu 数据长度，跳过整个 nalu（含起始码）
        uint64_t naluEnd = range.pos.value_or(0) + static_cast<uint64_t>(range.size.value_or(0));
        view = view.substr(naluEnd);
        range.shift(static_cast<int64_t>(globalPos));
        globalPos += naluEnd;
        naluRanges.push_back(range);
    }
    return naluRanges;
}

void TSDemuxer::updateH264HeaderInfo(uint8_t naluType, DataView dataView) noexcept {
    if (naluType == 7) {
        if (!videoInfo_->sps || videoInfo_->sps->view() != dataView) {
            parseH264Sps(dataView, videoInfo_);
            videoInfo_->naluHeaderLength = 4;
            videoInfo_->sps = std::make_shared<Data>();
            videoInfo_->sps->append(dataView);
            parseInfo_.isNotifiedHeader = false;
        }
    } else if (naluType == 8) {
        if (!videoInfo_->pps || videoInfo_->pps->view() != dataView) {
            videoInfo_->pps = std::make_shared<Data>();
            videoInfo_->pps->append(dataView);
            parseInfo_.isNotifiedHeader = false;
        }
    }
}

std::shared_ptr<VideoFrameInfo> TSDemuxer::makeVideoFrameInfo(uint8_t naluType,
                                                              uint8_t sliceType) noexcept {
    auto info = std::make_shared<VideoFrameInfo>();
    info->width = videoInfo_->width;
    info->height = videoInfo_->height;
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
    return info;
}

bool TSDemuxer::packH264VideoPacket(uint32_t tsIndex, AVFramePtrArray& frames) noexcept {
    if (videoESFrame_.mediaData.empty()) {
        LogI("media data is empty!");
        return false;
    }

    auto view = videoESFrame_.mediaData.view();
    auto naluRanges = collectNaluRanges(view);
    for (const auto& range : naluRanges) {
        auto dataView =
            view.substr(range.pos.value_or(0), static_cast<uint64_t>(range.size.value_or(0)));
        auto naluType = static_cast<uint8_t>(static_cast<uint8_t>(dataView[0]) & 0x1f);

        if (naluType == 7 || naluType == 8) {
            updateH264HeaderInfo(naluType, dataView);
            continue;
        }
        if (naluType != 5 && naluType != 1) {
            continue;
        }

        auto [firstMBInSlice, sliceType] = parseAvcSliceType(dataView);
        if (firstMBInSlice != 0) {
            // multi-slice: append to last frame
            if (!frames.empty()) {
                writeVideoData(frames.back(), dataView);
            } else {
                LogE("frames array is empty for multi-slice!");
            }
            continue;
        }

        auto info = makeVideoFrameInfo(naluType, static_cast<uint8_t>(sliceType));
        auto frame = std::make_unique<AVFrame>(AVFrameType::Video);
        frame->info = info;
        frame->data = std::make_unique<Data>();
        writeVideoData(frame, dataView);
        frame->timeScale = 90000;
        frame->pts = videoESFrame_.pts;
        frame->dts = videoESFrame_.dts;
        applyTimestamp(*frame, parseInfo_.videoInfo, false);
        LogI("video, ts index:{} pts:{}", tsIndex, frame->pts);

        if (parseInfo_.calculatedFps == 0) {
            if (frame->index != 0) {
                frame->duration = static_cast<uint32_t>(
                    (frame->ptsTime() / static_cast<double>(frame->index)) * 1000);
                parseInfo_.calculatedFps =
                    static_cast<uint32_t>(round(1000.0 / static_cast<double>(frame->duration)));
            } else {
                frame->duration = 33; // default 33ms
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
    // Offset to handle wraparound
    auto& fixInfo = isAudio ? audioFixInfo_ : videoFixInfo_;
    if (dts < fixInfo.preDtsTime) {
        fixInfo.offset += TSPtsFixInfo::kMaxPTS;
        LogI("dts wraparound:{}, {}, offset:{}", dts, fixInfo.preDtsTime, fixInfo.offset);
    }
    fixInfo.preDtsTime = dts;
    dts += fixInfo.offset;
    pts += fixInfo.offset;
}

bool findAdtsHeader(DataView view, uint32_t& pos) noexcept {
    pos = 0;
    while (view.length() >= 7) {
        if (view[0] == 0xff && (view[1] & 0xf0) == 0xf0) {
            return true;
        }
        pos++;
        view = view.substr(1);
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
    header.hasCRC = !(dataView[1] & 0x1); // 1 no CRC data
    header.profile = (dataView[2] >> 6) & 0x3;
    header.samplingIndex = (dataView[2] >> 2) & 0xF;
    header.channel = static_cast<uint8_t>(((dataView[2] & 0x1) << 2) | ((dataView[3] >> 6) & 0x3));
    header.frameLength = static_cast<uint16_t>(((dataView[3] & 0x3) << 11) | (dataView[4] << 3) |
                                               (dataView[5] >> 5));
    header.bufferFullness =
        static_cast<uint16_t>(((dataView[5] & 0x1F) << 6) | ((dataView[6] >> 2) & 0x3f));
    header.blockCount = dataView[6] & 0x3;
    return true;
}

void TSDemuxer::resetData() noexcept {
    audioFixInfo_.reset();
    videoFixInfo_.reset();
    videoESFrame_.reset();
    audioESFrame_.reset();
    // seek 后需要重置解析状态，否则 fps 计算和 header 通知会使用旧数据
    parseInfo_.reset();
    LogI("[seek info] reset data");
}

void TSDemuxer::applyTimestamp(AVFrame& frame, TSParseInfo::Info& info, bool isAudio) noexcept {
    frame.index = info.frameIndex++;
    recalculatePtsDts(frame.pts, frame.dts, isAudio);
    if (!info.isValid) {
        info.firstPts = frame.pts;
        info.firstDts = frame.dts;
        info.isValid = true;
    }
    frame.pts -= info.firstPts;
    frame.dts -= info.firstDts;
}

void TSDemuxer::initAudioInfoIfNeeded(const AACAdtsHeader& header, uint64_t sampleRate) noexcept {
    if (!audioInfo_ || parseInfo_.audioInfo.isValid) {
        return;
    }
    audioInfo_->channels = header.channel;
    audioInfo_->profile = getAudioProfile(header.profile + 1); // adts profile starts at 0
    audioInfo_->sampleRate = sampleRate;
    audioInfo_->timeScale = 90000;
    audioInfo_->bitsPerSample = 16;
    audioInfo_->samplingFrequencyIndex = header.samplingIndex;
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
        auto sampleRate = static_cast<uint64_t>(getAACSamplingRate(header.samplingIndex));
        auto dataView = view.substr(
            static_cast<uint64_t>(headerLength),
            static_cast<uint64_t>(header.frameLength) - static_cast<uint64_t>(headerLength));
        auto info = std::make_shared<AudioFrameInfo>();
        info->channels = header.channel;
        info->sampleRate = sampleRate;
        info->refIndex = tsIndex;
        auto frame = std::make_unique<AVFrame>(AVFrameType::Audio);
        frame->data = std::make_unique<Data>(dataView);
        frame->duration = 1024 * 1000 / sampleRate; // ms
        frame->info = info;
        frame->timeScale = 90000;
        frame->pts = audioESFrame_.pts;
        frame->dts = audioESFrame_.dts;
        initAudioInfoIfNeeded(header, sampleRate);
        applyTimestamp(*frame, parseInfo_.audioInfo, true);
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
    if (videoInfo_ && !parseInfo_.isNotifiedHeader && parseInfo_.isReady()) {
        if (videoInfo_->fps == 0) {
            videoInfo_->fps = static_cast<uint16_t>(parseInfo_.calculatedFps);
        }
        parseInfo_.isNotifiedHeader = true;
        result.resultCode =
            isNotifiedHeader ? DemuxerResultCode::VideoInfoChange : DemuxerResultCode::ParsedHeader;
    } else if (videoInfo_ && videoInfo_->fps == 0 && parseInfo_.isNotifiedHeader) {
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
        auto tsResult = StringUtil::fromChars<uint64_t>(packet.tag);
        if (!tsResult) {
            LogE("parse ts index failed: {}", packet.tag);
            result.resultCode = DemuxerResultCode::Failed;
            return result;
        }
        tsIndex = *tsResult;
    } else {
        LogI("data packet tag is empty!");
    }

    if (seekTsIndex_ != kInvalidTSIndex && seekTsIndex_ != tsIndex) {
        LogI("[seek info], seek index:{} expired data:{},", seekTsIndex_, tsIndex);
        return result;
    } else {
        seekTsIndex_ = kInvalidTSIndex; // reset
    }

    if (!buffer_->append(static_cast<uint64_t>(packet.offset), std::move(packet.data))) {
        LogE("discard data, ts index:{}, data offset:{}, parse offset:{}",
             tsIndex,
             packet.offset,
             buffer_->offset());
        result.resultCode = DemuxerResultCode::InvalidData;
        return result;
    }

    if (!tsDemuxer_->parseData(*buffer_, static_cast<uint32_t>(tsIndex), result)) {
        LogE("parse ts data error! offset:{}, ts index:{}", packet.offset, tsIndex);
        result.resultCode = DemuxerResultCode::Failed;
    }
    if (tsIndex == getTSInfos().size() - 1 && buffer_->empty()) {
        isCompleted_ = true;
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
    buffer_->append(buffer->detachData());
    auto content = buffer_->view().toString();

    if (!mainParser_->isCompleted()) {
        if (!mainParser_->parse(content)) {
            LogI("need more data.");
            return false;
        }
        LogI("parser hls info complete:{}",
             mainParser_->isMasterPlaylist() ? "play list" : "normal");
        if (mainParser_->isMasterPlaylist()) {
            slinkParser_ = std::make_unique<M3U8Parser>(baseUrl_);
        } else {
            totalDuration_ = CTime(mainParser_->playlist()->totalDuration());
            isOpened_ = true;
        }
    } else if (mainParser_->isMasterPlaylist()) {
        if (!slinkParser_->parse(content)) {
            LogI("need more data.");
            return false;
        }
        LogI("parser hls info complete");
        totalDuration_ = CTime(slinkParser_->playlist()->totalDuration());
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
        auto pos = infos[tsIndex].byteRange.has_value() ? infos[tsIndex].byteRange->pos.value_or(0)
                                                        : uint64_t{0};
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
    auto comp = [time](const auto& segment) {
        return segment.startTime <= time && time <= segment.endTime();
    };
    auto& infos = getTSInfos();
    auto it = std::find_if(infos.begin(), infos.end(), comp);
    if (it == infos.end()) {
        return kInvalidTSIndex;
    }
    return static_cast<uint64_t>(it->index);
}

const std::vector<Segment>& HLSDemuxer::getTSInfos() const noexcept {
    SAssert(mainParser_ != nullptr, "demuxer closed");
    if (isPlayList()) {
        return slinkParser_->playlist()->segments();
    }
    return mainParser_->playlist()->segments();
}

} // namespace slark
