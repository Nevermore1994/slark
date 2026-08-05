//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//
#include "WavDemuxer.h"

#include <bit>

#include "AVFrame.hpp"
#include "DataView.hpp"
#include "IDemuxer.h"
#include "Log.hpp"
#include "MediaDefs.h"
#include "Util.h"

namespace slark {

static const std::string_view kWaveExtSubformat =
    "\x00\x00\x00\x00\x10\x00\x80\x00\x00\xAA\x00\x38\x9B\x71";

WAVDemuxer::WAVDemuxer() {
    type_ = DemuxerType::WAV;
}

WAVDemuxer::~WAVDemuxer() = default;

bool WAVDemuxer::parseExtensibleFormat(DataView formatSpec, FmtChunk& out) noexcept {
    uint16_t validBitsPerSample = 0;
    formatSpec.substr(18).readNumberLE(validBitsPerSample, 2);
    if (validBitsPerSample != out.bitsPerSample) {
        if (validBitsPerSample != 0) {
            LogE("validBits(%d) != bitsPerSample(%d) are not supported", validBitsPerSample,
                 out.bitsPerSample);
            return false;
        }
        LogW("WAVE_EXT has 0 valid bits per sample, ignoring");
    }

    formatSpec.substr(20).readNumberLE(out.channelMask, 4);
    LogI("numChannels=%d channelMask=0x%x", out.channels, out.channelMask);
    if ((out.channelMask >> 18) != 0) {
        LogE("invalid channel mask 0x%x", out.channelMask);
        return false;
    }
    if ((out.channelMask != 0) && (std::popcount(out.channelMask) != out.channels)) {
        LogE("invalid number of channels (%d) in channel mask (0x%x)",
             std::popcount(out.channelMask), out.channels);
        return false;
    }

    uint16_t extFormatValue = 0;
    formatSpec.substr(24).readNumberLE(extFormatValue, 2);
    auto extFormat = static_cast<WaveFormat>(extFormatValue);
    if (extFormat != WaveFormat::PCM && extFormat != WaveFormat::ALAW &&
        extFormat != WaveFormat::MULAW) {
        return false;
    }
    if (formatSpec.compare(26, 14, kWaveExtSubformat.data())) {
        LogE("unsupported GUID");
        return false;
    }
    return true;
}

bool WAVDemuxer::parseFmtChunk(DataView data,
                               uint64_t offset,
                               uint32_t chunkSize,
                               FmtChunk& out) noexcept {
    if (chunkSize < 16) {
        return false;
    }

    auto formatSpecData = data.substr(offset, 2);
    if (formatSpecData.length() < 2) {
        return false;
    }

    uint16_t formatValue = 0;
    formatSpecData.readNumberLE(formatValue, 2);
    out.format = static_cast<WaveFormat>(formatValue);
    if (out.format != WaveFormat::PCM && out.format != WaveFormat::ALAW &&
        out.format != WaveFormat::MULAW && out.format != WaveFormat::MSGSM &&
        out.format != WaveFormat::EXTENSIBLE) {
        return false;
    }

    uint8_t fmtSize = (out.format == WaveFormat::EXTENSIBLE) ? 40 : 16;
    auto formatSpec = data.substr(offset, fmtSize);
    if (formatSpec.length() < fmtSize) {
        return false;
    }

    if (!readFmtCommonFields(formatSpec, out)) {
        return false;
    }
    if (!validateBitsPerSample(out.format, out.bitsPerSample)) {
        return false;
    }
    if (out.format == WaveFormat::EXTENSIBLE) {
        return parseExtensibleFormat(formatSpec, out);
    }
    return true;
}

bool WAVDemuxer::readFmtCommonFields(DataView formatSpec, FmtChunk& out) noexcept {
    if (!formatSpec.substr(2).readNumberLE(out.channels, 2)) {
        return false;
    }
    if (out.channels < 1 || out.channels > 8) {
        return false;
    }
    if (out.format != WaveFormat::EXTENSIBLE && out.channels != 1 && out.channels != 2) {
        LogW("More than 2 channels (%d) in non-WAVE_EXT, unknown channel mask", out.channels);
    }

    uint32_t sampleRateValue = 0;
    formatSpec.substr(4).readNumberLE(sampleRateValue, 4);
    out.sampleRate = sampleRateValue;
    if (out.sampleRate == 0) {
        return false;
    }
    return formatSpec.substr(14).readNumberLE(out.bitsPerSample, 2);
}

bool WAVDemuxer::validateBitsPerSample(WaveFormat format, uint16_t bitsPerSample) noexcept {
    if (format == WaveFormat::PCM || format == WaveFormat::EXTENSIBLE) {
        return bitsPerSample == 8 || bitsPerSample == 16 || bitsPerSample == 24 ||
               bitsPerSample == 32;
    }
    if (format == WaveFormat::MSGSM) {
        return bitsPerSample == 0;
    }
    // WaveFormat::MULAW WaveFormat::ALAW
    return bitsPerSample == 8;
}

bool WAVDemuxer::open(std::unique_ptr<Buffer>& buffer) noexcept {
    if (!buffer || !buffer->require(12)) {
        return false;
    }
    auto probeData = buffer->view();
    if (probeData.compare(0, 4, "RIFF") || probeData.compare(8, 4, "WAVE")) {
        return false;
    }

    uint32_t chunkSize = 0;
    probeData.substr(4).readNumberLE(chunkSize, 4);

    auto offset = 12ull;
    auto remainSize = chunkSize;
    FmtChunk fmt;
    bool isValid = false;

    while (remainSize >= 8) {
        auto chunkHeader = probeData.substr(offset, 8);
        if (chunkHeader.length() < 8) {
            return false;
        }
        remainSize -= 8;
        offset += 8;

        uint32_t curChunkSize = 0;
        chunkHeader.substr(4).readNumberLE(curChunkSize, 4);
        if (curChunkSize > remainSize) {
            return false;
        }

        if (!chunkHeader.compare(0, 4, "fmt ")) {
            if (!parseFmtChunk(probeData, offset, curChunkSize, fmt)) {
                return false;
            }
            isValid = true;
        } else if (!chunkHeader.compare(0, 4, "data")) {
            if (!isValid) {
                continue;
            }

            int64_t durationUs = 0;
            if (fmt.format == WaveFormat::MSGSM) {
                durationUs = static_cast<int64_t>(1000000 * (curChunkSize / 65 * 320) / 8000);
            } else {
                size_t bytesPerSample = fmt.bitsPerSample >> 3;
                auto bitSample = fmt.channels * bytesPerSample;
                durationUs =
                    static_cast<int64_t>(1000000LL * (curChunkSize / bitSample) / fmt.sampleRate);
            }

            totalDuration_ = CTime(static_cast<double>(durationUs) / 1000000.0);
            audioInfo_ = std::make_unique<AudioInfo>();
            audioInfo_->channels = fmt.channels;
            audioInfo_->sampleRate = fmt.sampleRate;
            audioInfo_->bitsPerSample = fmt.bitsPerSample;
            audioInfo_->mediaInfo = MEDIA_MIMETYPE_AUDIO_RAW;
            audioInfo_->timeScale = 1000000;
            headerInfo_ = std::make_unique<DemuxerHeaderInfo>();
            headerInfo_->dataSize = chunkSize + 8 - offset;
            headerInfo_->headerLength = offset;
            isOpened_ = true;
            buffer->skip(static_cast<int64_t>(offset));
            buffer_ = std::make_unique<Buffer>(headerInfo_->dataSize);
            return true;
        }

        offset += curChunkSize;
    }
    return false;
}

void WAVDemuxer::close() noexcept {
    reset();
}

uint64_t WAVDemuxer::getSeekToPos(double time) noexcept {
    if (!isOpened_) {
        return 0;
    }
    auto sampleCount =
        static_cast<uint64_t>(floor(time * static_cast<double>(audioInfo_->sampleRate)));
    return headerInfo_->headerLength + sampleCount * audioInfo_->bytePerSample();  // byte
}

AVFramePtr WAVDemuxer::makeAudioFrame(uint64_t dataLength,
                                      double scale,
                                      const std::shared_ptr<AudioFrameInfo>& frameInfo) noexcept {
    auto parsedLength = buffer_->pos();
    auto frame = std::make_unique<AVFrame>();
    frame->data = buffer_->readData(dataLength);
    frame->duration =
        static_cast<uint32_t>(static_cast<double>(frame->data->size()) / scale * 1000);
    frame->pts =
        static_cast<int64_t>(static_cast<double>(parsedLength) / scale * audioInfo_->timeScale);
    frame->dts = frame->pts;
    frame->index = ++parsedFrameCount_;
    frame->timeScale = audioInfo_->timeScale;
    frame->info = frameInfo;
    return frame;
}

DemuxerResult WAVDemuxer::parseData(DataPacket& packet) noexcept {
    if (!buffer_) {
        LogE("not init buffer");
        return {DemuxerResultCode::Failed, AVFramePtrArray(), AVFramePtrArray()};
    }
    if (packet.empty() || !isOpened_) {
        LogE("packet is empty!");
        return {DemuxerResultCode::Failed, AVFramePtrArray(), AVFramePtrArray()};
    }

    if (!buffer_->append(static_cast<uint64_t>(packet.offset), std::move(packet.data))) {
        return {DemuxerResultCode::Normal, AVFramePtrArray(), AVFramePtrArray()};
    }

    auto receivedLength = buffer_->pos() + buffer_->length();
    auto isCompleted = receivedLength >= headerInfo_->dataSize;

    LogA(audioInfo_->sampleRate != 0, "wav demuxer sample rate is invalid.");
    constexpr uint32_t kSamplesPerFrame = 1024;
    const uint64_t frameLength =
        audioInfo_->bitsPerSample * audioInfo_->channels * kSamplesPerFrame / 8;
    const double scale = static_cast<double>(audioInfo_->bitrate()) / 8;

    auto frameInfo = std::make_shared<AudioFrameInfo>();
    frameInfo->bitsPerSample = audioInfo_->bitsPerSample;
    frameInfo->channels = audioInfo_->channels;
    frameInfo->sampleRate = audioInfo_->sampleRate;

    AVFramePtrArray frameList;
    while (buffer_->length() >= frameLength) {
        frameList.push_back(makeAudioFrame(frameLength, scale, frameInfo));
    }

    DemuxerResultCode code = DemuxerResultCode::Normal;
    if (isCompleted) {
        frameList.push_back(makeAudioFrame(buffer_->length(), scale, frameInfo));
        isCompleted_ = true;
        LogI("file read completed.");
        code = DemuxerResultCode::FileEnd;
    }

    if (!frameList.empty()) {
        buffer_->shrink();
    }
    LogI("parsedLength {}, frame count {}", receivedLength, parsedFrameCount_);
    return {code, std::move(frameList), AVFramePtrArray()};
}

void WAVDemuxer::reset() noexcept {
    IDemuxer::reset();
    parsedFrameCount_ = 0;
}

}  // end namespace slark
