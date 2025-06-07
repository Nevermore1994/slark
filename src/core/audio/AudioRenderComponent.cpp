//
// Created by Nevermore on 2024/8/11.
// slark AudioRenderComponentImpl
// Copyright (c) 2024 Nevermore All rights reserved.
//

#include <memory>
#include <utility>
#include "AudioRenderComponent.h"
#include "Log.hpp"
#include "MediaBase.h"

namespace slark {

uint32_t calcAudioBufferSize(const std::shared_ptr<AudioInfo>& audioInfo) {
    if (!audioInfo) {
        LogE("audio info is nullptr");
        return 0;
    }
    // Calculate buffer size based on sample rate, channels, and bits per sample
    return static_cast<uint32_t>(
        kDefaultAudioBufferCacheTime *
            static_cast<double>(audioInfo->sampleRate * audioInfo->channels * 2)
    ); // default 16bit, 2 bytes per sample
}

AudioRenderComponent::AudioRenderComponent(std::shared_ptr<AudioInfo> info)
    : audioInfo_(std::move(info)) {
    init();
}

AudioRenderComponent::~AudioRenderComponent() {
    if (auto pimpl = pimpl_.load()) {
        pimpl->stop();
    }
    audioBuffer_.reset();
    pimpl_.reset();
    isFirstFrameRendered = false;
}

void AudioRenderComponent::init() noexcept {
    reset();
    if (!audioInfo_) {
        LogE("audio info is nullptr");
        return;
    }
    auto bufferSize = calcAudioBufferSize(audioInfo_);
    LogI("audio buffer size:{}", bufferSize);
    audioBuffer_ = std::make_unique<SyncRingBuffer<uint8_t>>(bufferSize);
    auto pimpl = createAudioRender(audioInfo_);
    pimpl->setProvider([this](uint8_t* data, uint32_t size, AudioDataFlag& flag) {
        if (audioBuffer_->read(data, size)) {
            if (!isFirstFrameRendered) {
                isFirstFrameRendered = true;
                if (firstFrameRenderCallBack) {
                    firstFrameRenderCallBack(Time::nowTimeStamp());
                }
            }
            LogI("request audio size:{}", size);
            return size;
        } else {
            LogE("request audio failed:{}", size);
        }
        return 0u;
    });
    pimpl_.reset(std::move(pimpl));
    isFirstFrameRendered = false;
}

bool AudioRenderComponent::send(AVFrameRefPtr frame) noexcept {
    return process(frame);
}

bool AudioRenderComponent::process(AVFrameRefPtr frame) noexcept {
    if (!frame || frame->data->empty()) {
        LogE("frame data is nullptr");
        return false;
    }
    return audioBuffer_->writeExact(
        frame->data->rawData,
        static_cast<uint32_t>(frame->data->length)
    );
}

void AudioRenderComponent::reset() noexcept {
    audioBuffer_.reset();
    isFirstFrameRendered = false;
    if (auto impl = pimpl_.load()) {
        impl->reset();
    }
}

std::shared_ptr<AudioInfo> AudioRenderComponent::audioInfo() const noexcept {
    SAssert(audioInfo_ != nullptr, "audio render info is nullptr");
    return audioInfo_;
}

void AudioRenderComponent::play() noexcept {
    if (auto pimpl = pimpl_.load()) {
        pimpl->play();
    } else {
        LogE("audio render is nullptr.");
    }
}

void AudioRenderComponent::pause() noexcept {
    if (auto pimpl = pimpl_.load()) {
        pimpl->pause();
    } else {
        LogE("audio render is nullptr.");
    }
}

void AudioRenderComponent::stop() noexcept {
    if (auto pimpl = pimpl_.load()) {
        pimpl->stop();
    } else {
        LogE("audio render is nullptr.");
    }
}

void AudioRenderComponent::setVolume(float volume) noexcept {
    if (auto pimpl = pimpl_.load()) {
        pimpl->setVolume(volume);
    } else {
        LogE("audio render is nullptr.");
    }
}

void AudioRenderComponent::flush() noexcept {
    if (auto pimpl = pimpl_.load()) {
        pimpl->flush();
    } else {
        LogE("audio render is nullptr.");
    }
}

void AudioRenderComponent::seek(double time) noexcept {
    audioBuffer_.reset();
    if (auto pimpl = pimpl_.load()) {
        pimpl->seek(time);
    } else {
        LogE("audio render is nullptr.");
    }
}

Time::TimePoint AudioRenderComponent::playedTime() noexcept {
    if (auto pimpl = pimpl_.load()) {
        return pimpl->playedTime();
    } else {
        return 0;
    }
}

}

