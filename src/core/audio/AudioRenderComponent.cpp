//
// Created by Nevermore on 2024/8/11.
// slark AudioRenderComponentImpl
// Copyright (c) 2024 Nevermore All rights reserved.
//

#include <memory>
#include <utility>
#include "AudioRenderComponent.h"
#include "Log.hpp"
#include "MediaUtil.h"

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
        if (auto pullDataFunc = pullDataFunc_.load(); pullDataFunc && !isFull_) {
            while (audioBuffer_->length() < size) {
                auto frame = std::invoke(*pullDataFunc, audioBuffer_->tail());
                if (frame == nullptr) {
                    break;
                }
                audioBuffer_->writeExact(
                    frame->data->rawData,
                    static_cast<uint32_t>(frame->data->length)
                );
            }
        }
        auto isSuccess = audioBuffer_->readExact(data, size);
        if (isSuccess && !isFirstFrameRendered) {
            isFirstFrameRendered = true;
            if (firstFrameRenderCallBack) {
                firstFrameRenderCallBack(Time::nowTimeStamp());
            }
        } 
        isHungry_ = !isSuccess;
        auto readSize = isSuccess ? size : 0u;
        LogI("request audio size:{}", readSize);
        isFull_ = false;
        return readSize;
    });
    pimpl_.reset(std::move(pimpl));
    LogI("init success");
    isFirstFrameRendered = false;
}

bool AudioRenderComponent::send(AVFrameRefPtr frame) noexcept {
    return process(frame);
}

bool AudioRenderComponent::process(AVFrameRefPtr frame) noexcept {
    if (!frame || !frame->data || frame->data->empty()) {
        LogE("frame data is nullptr");
        return false;
    }
    auto res = audioBuffer_->writeExact(
        frame->data->rawData,
        static_cast<uint32_t>(frame->data->length)
    );
    if (!res) {
        isFull_ = true;
    }
    return res;
}

void AudioRenderComponent::reset() noexcept {
    if (audioBuffer_) {
        audioBuffer_->reset();
    }
    isFirstFrameRendered = false;
    if (auto impl = pimpl_.load()) {
        impl->reset();
    }
}

std::shared_ptr<AudioInfo> AudioRenderComponent::audioInfo() const noexcept {
    SAssert(audioInfo_ != nullptr, "audio render info is nullptr");
    return audioInfo_;
}

void AudioRenderComponent::start() noexcept {
    if (auto pimpl = pimpl_.load()) {
        pimpl->play();
    } else {
        LogE("audio render is nullptr.");
    }
}

void AudioRenderComponent::pause() noexcept {
    if (auto pimpl = pimpl_.load()) {
        if (pimpl->status() == RenderStatus::Playing) {
            pimpl->pause();
        }
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
    if (audioBuffer_) {
        audioBuffer_->reset();
    }
    isFull_ = false;
    if (auto pimpl = pimpl_.load()) {
        pimpl->flush();
    } else {
        LogE("audio render is nullptr.");
    }
}

void AudioRenderComponent::seek(double time) noexcept {
    if (audioBuffer_) {
        audioBuffer_->reset();
    }
    isFull_ = false;
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

void AudioRenderComponent::renderEnd() noexcept {
    if (auto pimpl = pimpl_.load()) {
        pimpl->renderEnd();
    }
}

}

