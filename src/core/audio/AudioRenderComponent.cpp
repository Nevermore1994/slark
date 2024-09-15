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

AudioRenderComponent::AudioRenderComponent(std::shared_ptr<AudioInfo> info) : audioInfo_(std::move(info)) {
    init();
}

void AudioRenderComponent::init() noexcept {
    pimpl_.reset();
    pimpl_ = createAudioRender(audioInfo_);
    pimpl_->requestAudioData = [this](uint32_t size) {
        frames_.withWriteLock([this, size](auto& frames) {
            if (!audioBuffer_.isFull() && !frames.empty()) {
                while (audioBuffer_.tail() < frames.front()->data->length) {
                    audioBuffer_.append(frames.front()->data->rawData, frames.front()->data->length);
                    frames.pop_front();
                }
            }
            auto dataPtr = std::make_unique<Data>(size);
            auto tSize = audioBuffer_.read(dataPtr->rawData, size);
            dataPtr->length = tSize;
            if (tSize < size) {
               dataPtr->append(pullAudioData(size - tSize));
            }
            renderCompletion(renderPoint_);
            renderPoint_ += toDuration(*dataPtr, *audioInfo_);
            return dataPtr;
        });
        return nullptr;
    };
}

void AudioRenderComponent::receive(AVFrameRefPtr frame) noexcept {
    process(frame);
}

void AudioRenderComponent::process(AVFrameRefPtr frame) noexcept {
    frames_.withWriteLock([&](auto& frames){
        frames.push_back(frame);
        while (audioBuffer_.tail() < frames.front()->data->length) {
            audioBuffer_.append(frames.front()->data->rawData, frames.front()->data->length);
            frames.pop_front();
        }
    });
}

void AudioRenderComponent::clear() noexcept {
    frames_.withWriteLock([this](auto& frames) {
        audioBuffer_.reset();
        frames.clear();
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
    } else {
        LogE("audio render is nullptr.");
    }
}

void AudioRenderComponent::pause() noexcept {
    if (pimpl_) {
        pimpl_->pause();
    } else {
        LogE("audio render is nullptr.");
    }
}

void AudioRenderComponent::stop() noexcept {
    if (pimpl_) {
        pimpl_->stop();
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

}

