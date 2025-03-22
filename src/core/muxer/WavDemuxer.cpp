//
// Created by Nevermore on 2022/5/23.
// slark WAVDemuxer
// Copyright (c) 2022 Nevermore All rights reserved.
//
#include <any>
#include <string_view>
#include <bit>
#include "AVFrame.hpp"
#include "IDemuxer.h"
#include "Log.hpp"
#include "WavDemuxer.h"
#include "Util.hpp"
#include "MediaDefs.h"

namespace slark {

enum class WaveFormat {
    PCM = 0x0001, ALAW = 0x0006, MULAW = 0x0007, MSGSM = 0x0031, EXTENSIBLE = 0xFFFE
};

static const std::string_view WaveExtSubformat = "\x00\x00\x00\x00\x10\x00\x80\x00\x00\xAA\x00\x38\x9B\x71";

WAVDemuxer::WAVDemuxer() {
    type_ = DemuxerType::WAV;
}

WAVDemuxer::~WAVDemuxer() = default;

bool WAVDemuxer::open(std::unique_ptr<Buffer>& buffer) noexcept {
    bool res = false;
    if (!buffer || !buffer->require(12)) {
        return res;
    }
    auto probeData = buffer->view();
    if (probeData.compare(0, 4, "RIFF") || probeData.compare(8, 4, "WAVE")) {
        return res;
    }
    uint32_t totalSize = 0;
    Util::read4ByteLE(probeData.substr(4), totalSize);
    auto offset = 12ull;
    auto remainSize = totalSize;
    WaveFormat format = WaveFormat::PCM;
    uint16_t channels = 0;
    uint64_t sampleRate = 0;
    uint16_t bitsPerSample = 0;
    uint32_t channelMask = 0;
    bool isValid = false;
    uint64_t dataOffset = 0;
    uint64_t dataSize = 0;
    double duration = 0.0;
    auto completion = [&]() {
        totalDuration_ = CTime(duration / 1000.0);
        audioInfo_ = std::make_unique<AudioInfo>();
        audioInfo_->channels = channels;
        audioInfo_->sampleRate = sampleRate;
        audioInfo_->bitsPerSample = bitsPerSample;
        audioInfo_->mediaInfo = MEDIA_MIMETYPE_AUDIO_RAW;
        audioInfo_->timeScale = 1000000;
        headerInfo_ = std::make_unique<DemuxerHeaderInfo>();
        headerInfo_->dataSize = totalSize;
        headerInfo_->headerLength = offset;
        isOpened_ = true;
        buffer->skip(static_cast<int64_t>(offset));
        buffer_ = std::move(buffer);
    };
    while (remainSize >= 8) {
        auto chunkHeader = probeData.substr(offset, 8);
        if (chunkHeader.length() < 8) {
            return res;
        }

        remainSize -= 8;
        offset += 8;

        uint32_t chunkSize = 0;
        Util::read4ByteLE(chunkHeader.substr(4), chunkSize);

        if (chunkSize > remainSize) {
            return res;
        }

        if (!chunkHeader.compare(0, 4, "fmt ")) {
            if (chunkSize < 16) {
                return res;
            }

            auto formatSpecData = probeData.substr(offset, 2);
            if (formatSpecData.length() < 2) {
                return res;
            }
            
            uint16_t formatValue = 0;
            Util::read2ByteLE(formatSpecData, formatValue);
            format = static_cast<WaveFormat>(formatValue);
            if (format != WaveFormat::PCM && format != WaveFormat::ALAW && format != WaveFormat::MULAW &&
                format != WaveFormat::MSGSM && format != WaveFormat::EXTENSIBLE) {
                return res;
            }

            uint8_t fmtSize = 16;
            if (format == WaveFormat::EXTENSIBLE) {
                fmtSize = 40;
            }

            auto formatSpec = probeData.substr(offset, fmtSize);
            if (formatSpec.length() < fmtSize) {
                return res;
            }
            
            if (!Util::read2ByteLE(formatSpec.substr(2), channels)) {
                return res;
            }
            
            if (channels < 1 || channels > 8) {
                return res;
            }
            if (format != WaveFormat::EXTENSIBLE) {
                if (channels != 1 && channels != 2) {
                    LogW("More than 2 channels (%d) in non-WAVE_EXT, unknown channel mask", channels);
                }
            }
            
            uint32_t sampleRateValue = 0;
            Util::read4ByteLE(formatSpec.substr(4), sampleRateValue);
            sampleRate = sampleRateValue;
            if (sampleRate == 0) {
                return res;
            }

            Util::read2ByteLE(formatSpec.substr(14), bitsPerSample);

            if (format == WaveFormat::PCM || format == WaveFormat::EXTENSIBLE) {
                if (bitsPerSample != 8 && bitsPerSample != 16 && bitsPerSample != 24 && bitsPerSample != 32) {
                    return res;
                }
            } else if (format == WaveFormat::MSGSM && bitsPerSample != 0) {
                return res;
            } else if (bitsPerSample != 8) {
                //WaveFormat::MULAW WaveFormat::ALAW
                return res;
            } else if (format == WaveFormat::EXTENSIBLE) {
                uint16_t validBitsPerSample = 0;
                Util::read2ByteLE(formatSpec.substr(18), validBitsPerSample);
                if (validBitsPerSample != bitsPerSample) {
                    if (validBitsPerSample != 0) {
                        LogE("validBits(%d) != bitsPerSample(%d) are not supported", validBitsPerSample, bitsPerSample);
                        return res;
                    } else {
                        // we only support valitBitsPerSample == bitsPerSample but some WAV_EXT
                        // writers don't correctly set the valid bits value, and leave it at 0.
                        LogW("WAVE_EXT has 0 valid bits per sample, ignoring");
                    }
                }

                Util::read4ByteLE(formatSpec.substr(20), channelMask);
                LogI("numChannels=%d channelMask=0x%x", channels, channelMask);
                if ((channelMask >> 18) != 0) {
                    LogE("invalid channel mask 0x%x", channelMask);
                    return res;
                }

                if ((channelMask != 0) && (std::popcount(channelMask) != channels)) {
                    LogE("invalid number of channels (%d) in channel mask (0x%x)", std::popcount(channelMask),
                         channels);
                    return res;
                }

                // In a WAVE_EXT header, the first two bytes of the GUID stored at byte 24 contain
                // the sample format, using the same definitions as a regular WAV header
                uint16_t extFormatValue = 0;
                Util::read2ByteLE(formatSpec.substr(24), extFormatValue);
                auto extFormat = static_cast<WaveFormat>(extFormatValue);
                if (extFormat != WaveFormat::PCM && extFormat != WaveFormat::ALAW && extFormat != WaveFormat::MULAW) {
                    return res;
                }

                if (formatSpec.compare(26, 14, WaveExtSubformat.data())) {
                    LogE("unsupported GUID");
                    return res;
                }
            } else {
                return res;
            }
            isValid = true;
        } else if (!chunkHeader.compare(0, 4, "data")) {
            if (!isValid) {
                continue;
            }
            dataOffset = offset;
            dataSize = chunkSize;

            int64_t durationUs = 0;
            if (format == WaveFormat::MSGSM) {
                // 65 bytes pushFrameDecode to 320 8kHz samples
                durationUs = static_cast<int64_t>(1000000 * (dataSize / 65 * 320) / 8000);
            } else {
                size_t bytesPerSample = bitsPerSample >> 3;
                auto bitSample = channels * bytesPerSample;
                durationUs = static_cast<int64_t>(1000000LL * (dataSize / bitSample) / sampleRate);
            }
            duration = static_cast<double>(durationUs) / 1000.0;
            completion();
            return true;
        }

        offset += chunkSize;
    }
    return res;
}

void WAVDemuxer::close() noexcept {
    reset();
}

uint64_t WAVDemuxer::getSeekToPos(long double time) noexcept {
    if (!isOpened_) {
        return 0;
    }
    auto sampleCount = static_cast<uint64_t>(floor(time * static_cast<long double>(audioInfo_->sampleRate)));
    return headerInfo_->headerLength + sampleCount * audioInfo_->bytePerSample(); //byte
}

DemuxerResult WAVDemuxer::parseData(DataPacket& packet) noexcept {
    if (packet.empty() || !isOpened_) {
        return {DemuxerResultCode::Failed, AVFramePtrArray(), AVFramePtrArray()};
    }
    
    if (!buffer_->append(static_cast<uint64_t>(packet.offset), std::move(packet.data))) {
        return {DemuxerResultCode::Normal, AVFramePtrArray(), AVFramePtrArray()};
    }
    
    auto receivedLength = buffer_->pos() + buffer_->length();
    auto isCompleted = receivedLength >= headerInfo_->dataSize;
    //frame include 1024 sample
    constexpr uint16_t sampleCount = 1024;
    uint64_t frameLength = audioInfo_->bitsPerSample * audioInfo_->channels * sampleCount / 8;
    AVFramePtrArray frameList;
    SAssert(audioInfo_->sampleRate != 0, "wav demuxer sample rate is invalid.");
    auto scale = static_cast<double>(audioInfo_->bitrate()) / 8;
    auto frameInfo = std::make_shared<AudioFrameInfo>();
    frameInfo->bitsPerSample = audioInfo_->bitsPerSample;
    frameInfo->channels = audioInfo_->channels;
    frameInfo->sampleRate = audioInfo_->sampleRate;
    while (buffer_->length() >= frameLength) {
        auto prasedLength = buffer_->pos();
        auto frame = std::make_unique<AVFrame>();
        frame->data = buffer_->readData(frameLength);
        frame->duration = static_cast<uint32_t>(static_cast<double>(sampleCount) /
                                                static_cast<double>(audioInfo_->sampleRate) * 1000);
        frame->pts = static_cast<uint64_t>(static_cast<double>(prasedLength) / scale * audioInfo_->timeScale);
        frame->dts = frame->pts;
        frame->index = ++parsedFrameCount_;
        frame->timeScale = audioInfo_->timeScale;
        
        frame->info = frameInfo;
        frameList.push_back(std::move(frame));
    }
    DemuxerResultCode code = DemuxerResultCode::Normal;
    if (isCompleted) {
        auto prasedLength = buffer_->pos();
        auto frame = std::make_unique<AVFrame>();
        frame->data = buffer_->readData(buffer_->length());
        frame->duration = static_cast<uint32_t>(static_cast<double>(frame->data->length) / scale * 1000);
        frame->timeScale = audioInfo_->timeScale;
        frame->pts = static_cast<uint64_t>(ceil(static_cast<double>(prasedLength) / scale * audioInfo_->timeScale));
        frame->index = ++parsedFrameCount_;
        frame->info = frameInfo;
        frameList.push_back(std::move(frame));
        isCompleted_ = true;

        LogI("file read completed.");
        code = DemuxerResultCode::FileEnd;
    }
    if (!frameList.empty()) {
        buffer_->shrink();
    }
    LogI("prasedLength {}, frame count {}", receivedLength, parsedFrameCount_);
    return {code, std::move(frameList), AVFramePtrArray()};
}

void WAVDemuxer::reset() noexcept {
    IDemuxer::reset();
    parsedFrameCount_ = 0;
}

}//end namespace slark
