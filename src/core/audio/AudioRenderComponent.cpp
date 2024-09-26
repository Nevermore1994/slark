//
// Created by Nevermore on 2023/8/11.
// slark AudioRenderComponentImpl
// Copyright (c) 2023 Nevermore All rights reserved.
//
#include <memory>
#include <utility>
#include "AudioRenderComponent.h"
#include "Log.hpp"
#include "MediaUtility.hpp"

namespace slark::Audio {

AudioRenderComponent::AudioRenderComponent(std::shared_ptr<AudioInfo> info)
    : audioInfo_(std::move(info)) {
    init();
}

void AudioRenderComponent::init() noexcept {
    clock.reset();
    pimpl_.reset();
    pimpl_ = createAudioRender(audioInfo_);
    pimpl_->requestAudioData = [this](uint8_t* data, uint32_t size) {
        uint32_t tSize = 0;
        frames_.withWriteLock([this, size, data, &tSize](auto& frames) {
            if (!audioBuffer_.isFull() && !frames.empty()) {
                while (!frames.empty() && audioBuffer_.tail() >= frames.front()->data->length) {
                    audioBuffer_.append(frames.front()->data->rawData, frames.front()->data->length);
                    frames.pop_front();
                }
            }
            tSize = audioBuffer_.read(data, size);
            if (tSize < size && pullAudioData) {
                tSize += pullAudioData(data + tSize, size - tSize);
            }
            renderedDataLength_ += tSize;
        });
        clock.setTime(audioInfo_->dataLen2TimePoint(renderedDataLength_));
        LogI("request audio size:{}, render size:{}", size, tSize);
        return tSize;
    };
}

void AudioRenderComponent::send(AVFrameRefPtr frame) noexcept {
    process(frame);
}

void AudioRenderComponent::process(AVFrameRefPtr frame) noexcept {
    frames_.withWriteLock([&](auto& frames){
        frames.push_back(frame);
        while (!frames.empty() && audioBuffer_.tail() >= frames.front()->data->length) {
            audioBuffer_.append(frames.front()->data->rawData, frames.front()->data->length);
            frames.pop_front();
        }
    });
}

void AudioRenderComponent::clear() noexcept {
    frames_.withWriteLock([this](auto& frames) {
        audioBuffer_.reset();
        frames.clear();
        renderedDataLength_ = 0;
    });
}

void AudioRenderComponent::reset() noexcept {
    clear();
    init();
}

std::shared_ptr<AudioInfo> AudioRenderComponent::audioInfo() const noexcept {
    SAssert(audioInfo_ != nullptr, "audio render info is nullptr");
    return audioInfo_;
}

void AudioRenderComponent::play() noexcept {
    if (pimpl_) {
        pimpl_->play();
        clock.start();
    } else {
        LogE("audio render is nullptr.");
    }
}

void AudioRenderComponent::pause() noexcept {
    if (pimpl_) {
        pimpl_->pause();
        clock.pause();
    } else {
        LogE("audio render is nullptr.");
    }
}

void AudioRenderComponent::stop() noexcept {
    if (pimpl_) {
        pimpl_->stop();
        clock.reset();
    } else {
        LogE("audio render is nullptr.");
    }
}

void AudioRenderComponent::setVolume(float volume) noexcept {
    if (pimpl_) {
        pimpl_->setVolume(volume);
    } else {
        LogE("audio render is nullptr.");
    }
}

void AudioRenderComponent::flush() noexcept {
    if (pimpl_) {
        pimpl_->flush();
    } else {
        LogE("audio render is nullptr.");
    }
}

void AudioRenderComponent::seek(Time::TimePoint time) noexcept {
    frames_.withWriteLock([this, time](auto& frames) {
        frames.clear();
        audioBuffer_.reset();
        renderedDataLength_ = audioInfo_->timePoint2DataLen(time);
        LogI("audio render seek to pos:{}", renderedDataLength_);
    });
    clock.setTime(time);
}

}

