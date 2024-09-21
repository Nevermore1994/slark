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
#include "MediaUtility.hpp"
#include "MediaDefs.h"

namespace slark {

enum class WaveFormat {
    PCM = 0x0001, ALAW = 0x0006, MULAW = 0x0007, MSGSM = 0x0031, EXTENSIBLE = 0xFFFE
};

static const std::string_view WaveextSubformat = "\x00\x00\x00\x00\x10\x00\x80\x00\x00\xAA\x00\x38\x9B\x71";

WAVDemuxer::WAVDemuxer() = default;

WAVDemuxer::~WAVDemuxer() = default;

std::tuple<bool, uint64_t> WAVDemuxer::open(std::string_view probeData) noexcept {
    auto res = std::make_tuple(false, 0);
    if (probeData.length() < 12) {
        return res;
    }

    if (probeData.compare(0, 4, "RIFF") || probeData.compare(8, 4, "WAVE")) {
        return res;
    }
    auto totalSize = uint32LE(&probeData[4]);
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
        totalDuration_ = CTime(duration);
        audioInfo_ = std::make_unique<Audio::AudioInfo>();
        audioInfo_->channels = channels;
        audioInfo_->sampleRate = sampleRate;
        audioInfo_->bitsPerSample = bitsPerSample;
        audioInfo_->mediaInfo = MEDIA_MIMETYPE_AUDIO_RAW;
        headerInfo_ = std::make_unique<DemuxerHeaderInfo>();
        headerInfo_->dataSize = totalSize;
        headerInfo_->headerLength = offset;
        isInited_ = true;
    };
    while (remainSize >= 8) {
        auto chunkHeader = probeData.substr(offset, 8);
        if (chunkHeader.length() < 8) {
            return res;
        }

        remainSize -= 8;
        offset += 8;

        uint32_t chunkSize = uint32LE(&chunkHeader[4]);

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

            format = static_cast<WaveFormat>(uint16LE(&formatSpecData[0]));
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

            channels = uint16LE(&formatSpec[2]);
            if (channels < 1 || channels > 8) {
                return res;
            }
            if (format != WaveFormat::EXTENSIBLE) {
                if (channels != 1 && channels != 2) {
                    LogW("More than 2 channels (%d) in non-WAVE_EXT, unknown channel mask", channels);
                }
            }

            sampleRate = uint32LE(&formatSpec[4]);

            if (sampleRate == 0) {
                return res;
            }

            bitsPerSample = uint16LE(&formatSpec[14]);

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
                auto validBitsPerSample = uint16LE(&formatSpec[18]);
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

                channelMask = uint32LE(&formatSpec[20]);
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
                auto extFormat = static_cast<WaveFormat>(uint16LE(&formatSpec[24]));
                if (extFormat != WaveFormat::PCM && extFormat != WaveFormat::ALAW && extFormat != WaveFormat::MULAW) {
                    return res;
                }

                if (formatSpec.compare(26, 14, WaveextSubformat.data())) {
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
                // 65 bytes decode to 320 8kHz samples
                durationUs = static_cast<int64_t>(1000000 * (dataSize / 65 * 320) / 8000);
            } else {
                size_t bytesPerSample = bitsPerSample >> 3;
                auto bitSample = channels * bytesPerSample;
                durationUs = static_cast<int64_t>(1000000LL * (dataSize / bitSample) / sampleRate);
            }
            duration = static_cast<double>(durationUs) / 1000.0;
            completion();
            return {isValid, dataOffset};
        }

        offset += chunkSize;
    }
    return res;
}

void WAVDemuxer::close() noexcept {
    reset();
}

uint64_t WAVDemuxer::getSeekToPos(CTime time) {
    if (!isInited_) {
        return 0;
    }
    return static_cast<uint64_t>(static_cast<long double>(audioInfo_->bitrate() / 8)  * time.second()); //byte
}

DemuxerResult WAVDemuxer::parseData(std::unique_ptr<Data> data) {
    if (data->empty() || !isInited_) {
        return {DemuxerResultCode::Failed, AVFrameArray(), AVFrameArray()};
    }

    if (overflowData_ == nullptr) {
        overflowData_ = std::make_unique<Data>();
        parsedLength_ += headerInfo_->headerLength;
    }
    parsedLength_ += data->length;
    overflowData_->append(std::move(data));
    //frame include 1024 sample
    constexpr uint16_t sampleCount = 1024;
    uint64_t frameLength = audioInfo_->bitsPerSample * audioInfo_->channels * sampleCount / 8;
    AVFrameArray frameList;
    uint64_t start = 0;
    auto isCompleted = parsedLength_ >= headerInfo_->dataSize;
    SAssert(audioInfo_->sampleRate != 0, "wav demuxer sample rate is invalid.");
    while (overflowData_->length - start + 1 >= frameLength) {
        auto frame = std::make_unique<AVFrame>();
        frame->data = overflowData_->copy(start, static_cast<int64_t>(frameLength));
        frame->duration = static_cast<uint32_t>(static_cast<double>(sampleCount) /
                                                static_cast<double>(audioInfo_->sampleRate) * 1000);
        frame->index = ++parseFrameCount_;
        frame->info = std::make_any<AudioFrameInfo>();
        auto& frameInfo = std::any_cast<AudioFrameInfo&>(frame->info);
        frameInfo.bitsPerSample = audioInfo_->bitsPerSample;
        frameInfo.channels = audioInfo_->channels;
        frameInfo.sampleRate = audioInfo_->sampleRate;
        frameList.push_back(std::move(frame));

        start += frameLength;
    }
    DemuxerResultCode code = DemuxerResultCode::Normal;
    auto remainLen = static_cast<int64_t>(overflowData_->length - start);
    if (start > 0 && !isCompleted) {
        auto t = overflowData_->copy(start, remainLen);
        LogI("packed data length: {}, overflow length: {}", start, t->length);
        overflowData_ = std::move(t);
    } else if (isCompleted) {
        auto frame = std::make_unique<AVFrame>();
        frame->data = overflowData_->copy(start, remainLen);
        auto scale = static_cast<double>(audioInfo_->sampleRate * audioInfo_->bitsPerSample * audioInfo_->channels);
        frame->duration = static_cast<uint32_t>(static_cast<double>(frame->data->length) / scale * 1000);
        frame->index = ++parseFrameCount_;
        frameList.push_back(std::move(frame));

        LogI("file read completed.");
        code = DemuxerResultCode::FileEnd;
    }
    LogI("demux data {}, overflow data {}, frame count {}", parsedLength_, overflowData_->length, parseFrameCount_);
    return {code, std::move(frameList), AVFrameArray()};
}

}//end namespace slark