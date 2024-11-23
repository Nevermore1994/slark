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

namespace slark {

AudioRenderComponent::AudioRenderComponent(std::shared_ptr<AudioInfo> info)
    : audioInfo_(std::move(info)) {
    init();
}

void AudioRenderComponent::init() noexcept {
    clock_.reset();
    pimpl_.reset();
    pimpl_ = createAudioRender(audioInfo_);
    pimpl_->requestAudioData = [this](uint8_t* data, uint32_t size) {
        std::unique_lock<std::mutex> lock(renderMutex_);
        uint32_t tSize = 0;
        frames_.withWriteLock([this, size, data, &tSize](auto& frames) {
            tSize = audioBuffer_.read(data, size);
            if (tSize < size && !frames.empty()) {
                while (!frames.empty() && audioBuffer_.tail() >= frames.front()->data->length) {
                    auto length = static_cast<uint32_t>(frames.front()->data->length);
                    audioBuffer_.append(frames.front()->data->rawData, length);
                    frames.pop_front();
                }
                auto secondReadSize = audioBuffer_.read(data + tSize, size - tSize);
                tSize += secondReadSize;
            }
            renderedDataLength_ += tSize;
        });
        ///TODO: Fix audio data latency
        if (tSize != 0) {
            clock_.setTime(audioInfo_->dataLen2TimePoint(renderedDataLength_));
            LogI("[seek info]push audio frame render:{}", clock_.time().second());
        }
        if (!isFirstFrameRendered && tSize > 0) {
            isFirstFrameRendered = true;
            if (firstFrameRenderCallBack) {
                firstFrameRenderCallBack(Time::nowTimeStamp());
            }
        }
        LogI("request audio size:{}, render size:{}", size, tSize);
        return tSize;
    };
    isFirstFrameRendered = false;
}

void AudioRenderComponent::send(AVFrameRefPtr frame) noexcept {
    process(frame);
}

void AudioRenderComponent::process(AVFrameRefPtr frame) noexcept {
    frames_.withWriteLock([&](auto& frames){
        frames.push_back(frame);
        while (!frames.empty() && audioBuffer_.tail() >= frames.front()->data->length) {
            auto length = static_cast<uint32_t>(frames.front()->data->length);
            audioBuffer_.append(frames.front()->data->rawData, length);
            if (renderedDataLength_ == 0) {
                renderedDataLength_ = audioInfo_->timePoint2DataLen(static_cast<uint64_t>(frames.front()->ptsTime() * Time::kMicroSecondScale));
                LogI("[seek info]audio render set pos:{}", renderedDataLength_);
            }
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
    clock_.reset();
}

std::shared_ptr<AudioInfo> AudioRenderComponent::audioInfo() const noexcept {
    SAssert(audioInfo_ != nullptr, "audio render info is nullptr");
    return audioInfo_;
}

void AudioRenderComponent::play() noexcept {
    if (pimpl_) {
        pimpl_->play();
        clock_.start();
    } else {
        LogE("audio render is nullptr.");
    }
}

void AudioRenderComponent::pause() noexcept {
    if (pimpl_) {
        pimpl_->pause();
        clock_.pause();
    } else {
        LogE("audio render is nullptr.");
    }
}

void AudioRenderComponent::stop() noexcept {
    if (pimpl_) {
        pimpl_->stop();
        clock_.reset();
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
    std::unique_lock<std::mutex> lock(renderMutex_);
    flush();
    frames_.withWriteLock([this, time](auto& frames) {
        frames.clear();
        audioBuffer_.reset();
        renderedDataLength_ = 0;
    });
    clock_.setTime(time);
}

}

