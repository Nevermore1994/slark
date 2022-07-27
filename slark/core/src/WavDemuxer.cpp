//
// Created by Nevermore on 2022/5/23.
// slark WAVDemuxer
// Copyright (c) 2022 Nevermore All rights reserved.
//
#include <string_view>
#include "Log.hpp"
#include "WavDemuxer.hpp"
#include "MediaUtility.hpp"
#include "Time.hpp"

using namespace slark;

enum class WaveFormat {
    PCM = 0x0001,
    ALAW = 0x0006,
    MULAW = 0x0007,
    MSGSM = 0x0031,
    EXTENSIBLE = 0xFFFE
};

static const char *WAVEEXT_SUBFORMAT = "\x00\x00\x00\x00\x10\x00\x80\x00\x00\xAA\x00\x38\x9B\x71";

WAVDemuxer::WAVDemuxer() {

}

WAVDemuxer::~WAVDemuxer() {

}

std::tuple<bool, uint64_t> WAVDemuxer::open(std::string_view probeData) noexcept {
    auto res = std::make_tuple(false, 0);
    if(probeData.length() < 12) {
        return res;
    }
    
    if(probeData.compare(0, 4, "RIFF") || probeData.compare(8, 4, "WAVE")) {
        return res;
    }
    auto totalSize = uint32LE(&probeData[4]);
    auto offset = 12ull;
    auto remainSize = totalSize;
    WaveFormat format;
    uint16_t channels = 0;
    uint64_t sampleRate = 0;
    uint16_t bitsPerSample = 0;
    uint32_t channelMask = 0;
    bool isValid = false;
    uint64_t dataOffset = 0;
    uint64_t dataSize = 0;
    double duration = 0.0;
    auto completion = [&](){
        audioInfo_ = std::make_unique<AudioInfo>();
        audioInfo_->channels = channels;
        audioInfo_->sampleRate = sampleRate;
        audioInfo_->bitsPerSample = bitsPerSample;
        audioInfo_->duration = CTime(duration, 1000000);
        headerInfo_.dataSize = totalSize;
        headerInfo_.headerLength = offset;
        isInited_ = true;
    };
    while(remainSize >= 8) {
        auto chunkHeader = probeData.substr(offset, 8);
        if(chunkHeader.length() < 8) {
            return res;
        }
        
        remainSize -= 8;
        offset += 8;
        
        uint32_t chunkSize = uint32LE(&chunkHeader[4]);
        
        if(chunkSize > remainSize) {
            return res;
        }
        
        if(!chunkHeader.compare(0, 4, "fmt ")) {
            if(chunkSize < 16) {
                return res;
            }
            
            auto formatSpecData = probeData.substr(offset, 2);
            if(formatSpecData.length() < 2) {
                return res;
            }
            
            format = static_cast<WaveFormat>(uint16LE(&formatSpecData[0]));
            if(format != WaveFormat::PCM && format != WaveFormat::ALAW && format != WaveFormat::MULAW &&
               format != WaveFormat::MSGSM && format != WaveFormat::EXTENSIBLE) {
                return res;
            }
            
            uint8_t fmtSize = 16;
            if(format == WaveFormat::EXTENSIBLE) {
                fmtSize = 40;
            }
            
            auto formatSpec = probeData.substr(offset, fmtSize);
            if(formatSpec.length() < fmtSize) {
                return res;
            }
            
            channels = uint16LE(&formatSpec[2]);
            if(format != WaveFormat::EXTENSIBLE) {
                if(channels != 1 && channels != 2) {
                    logw("More than 2 channels (%d) in non-WAVE_EXT, unknown channel mask", channels);
                }
            } else if(channels < 1 && channels > 8) {
                return res;
            }
            
            sampleRate = uint32LE(&formatSpec[4]);
            
            if(sampleRate == 0) {
                return res;
            }
            
            bitsPerSample = uint16LE(&formatSpec[14]);
            
            if(format == WaveFormat::PCM || format == WaveFormat::EXTENSIBLE) {
                if(bitsPerSample != 8 && bitsPerSample != 16 && bitsPerSample != 24) {
                    return res;
                }
            } else if(format == WaveFormat::MSGSM && bitsPerSample != 0) {
                return res;
            } else if(bitsPerSample != 8) {
                //WaveFormat::MULAW WaveFormat::ALAW
                return res;
            }
            
            if(format == WaveFormat::EXTENSIBLE) {
                auto validBitsPerSample = uint16LE(&formatSpec[18]);
                if(validBitsPerSample != bitsPerSample) {
                    if(validBitsPerSample != 0) {
                        loge("validBits(%d) != bitsPerSample(%d) are not supported",
                             validBitsPerSample, bitsPerSample);
                        return res;
                    } else {
                        // we only support valitBitsPerSample == bitsPerSample but some WAV_EXT
                        // writers don't correctly set the valid bits value, and leave it at 0.
                        logw("WAVE_EXT has 0 valid bits per sample, ignoring");
                    }
                }
                
                channelMask = uint32LE(&formatSpec[20]);
                logi("numChannels=%d channelMask=0x%x", channels, channelMask);
                if((channelMask >> 18) != 0) {
                    loge("invalid channel mask 0x%x", channelMask);
                    return res;
                }
                
                if((channelMask != 0)
                   && (popcount(channelMask) != channels)) {
                    loge("invalid number of channels (%d) in channel mask (0x%x)",
                         popcount(channelMask), channels);
                    return res;
                }
                
                // In a WAVE_EXT header, the first two bytes of the GUID stored at byte 24 contain
                // the sample format, using the same definitions as a regular WAV header
                auto extFormat = static_cast<WaveFormat>(uint16LE(&formatSpec[24]));
                if(extFormat != WaveFormat::PCM && extFormat != WaveFormat::ALAW && extFormat != WaveFormat::MULAW) {
                    return res;
                }
                
                if(formatSpec.compare(26, 14, WAVEEXT_SUBFORMAT)) {
                    loge("unsupported GUID");
                    return res;
                }
            }
            isValid = true;
        } else if(!chunkHeader.compare(0, 4, "data")) {
            if(!isValid) {
                continue;
            }
            dataOffset = offset;
            dataSize = chunkSize;
            
            int64_t durationUs = 0;
            if(format == WaveFormat::MSGSM) {
                // 65 bytes decode to 320 8kHz samples
                durationUs = 1000000LL * (dataSize / 65 * 320) / 8000;
            } else {
                size_t bytesPerSample = bitsPerSample >> 3;
                durationUs = 1000000LL * (dataSize / (channels * bytesPerSample)) / sampleRate;
            }
            duration = durationUs / 1000.0;
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

void WAVDemuxer::reset() noexcept {
    isInited_ = false;
    isCompleted_ = false;
    parseLength_ = 0;
    overflowData_->release();
}

std::tuple<DemuxerState, AVFrameList> WAVDemuxer::parseData(std::unique_ptr<Data> data) {
    if(data->empty()) {
        return {DemuxerState::Failed, AVFrameList()};
    }
    if(overflowData_ == nullptr){
        overflowData_ = std::make_unique<Data>();
        parseLength_ += headerInfo_.headerLength;
    }
    parseLength_ += data->length;
    overflowData_->append(*data);
    //frame include 2048 sample
    constexpr uint16_t sampleCount = 2048;
    auto frameLength = audioInfo_->bitsPerSample * audioInfo_->channels * sampleCount;
    AVFrameList frameList;
    auto start = 0ll;
    auto isCompleted = parseLength_ >= headerInfo_.dataSize;
    assert(audioInfo_->sampleRate != 0);
    while(overflowData_->length - start + 1 >= frameLength) {
        auto frame = std::make_unique<AVFrame>();
        frame->data = overflowData_->copy(start, frameLength);
        frame->duration = static_cast<double>(sampleCount) / audioInfo_->sampleRate * 1000;
        frame->index = ++parseFrameCount_;
        frameList.push_back(std::move(frame));
        
        start += frameLength + 1;
    }
    DemuxerState state = DemuxerState::Failed;
    if(start > 0 && !isCompleted) {
        auto t = overflowData_->copyPtr(start, overflowData_->length - start + 1);
       // logi("demux data %lld, overflow length %lld", start, t->length);
        overflowData_ = std::move(t);
        state = DemuxerState::Success;
    } else if(isCompleted) {
        auto t = start == 0 ? 0 : 1;
        auto frame = std::make_unique<AVFrame>();
        frame->data = overflowData_->copy(start, overflowData_->length - start + t);
        frame->duration = static_cast<double>(frame->data.length) /
                          (audioInfo_->sampleRate * audioInfo_->bitsPerSample * audioInfo_->channels) * 1000;
        frame->index = ++parseFrameCount_;
        frameList.push_back(std::move(frame));
        
        logi("demux completed.");
        isCompleted_ = true;
        state = DemuxerState::FileEnd;
    }
    logi("demuxed data %lld, overflow data %lld, frame count %ld", parseLength_, overflowData_->length, parseFrameCount_);
    return {state, std::move(frameList)};
}

