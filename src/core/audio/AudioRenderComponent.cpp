//
// Created by Nevermore on 2023/8/11.
// slark AudioRenderComponentImpl
// Copyright (c) 2023 Nevermore All rights reserved.
//

#include <memory>
#include <utility>
#include "AudioRenderComponent.h"
#include "Log.hpp"
#include "MediaBase.h"

namespace slark {

AudioRenderComponent::AudioRenderComponent(std::shared_ptr<AudioInfo> info)
    : audioInfo_(std::move(info)) {
    init();
}

void AudioRenderComponent::init() noexcept {
    reset();
    auto pimpl = createAudioRender(audioInfo_);
    pimpl->requestAudioData = [this](uint8_t* data, uint32_t size, AudioDataFlag& flag) {
        uint32_t tSize = 0;
        frames_.withWriteLock([this, size, data, &tSize](auto& frames) {
            tSize = audioBuffer_.read(data, size);
            if (tSize < size && !frames.empty()) {
                while (!frames.empty() && audioBuffer_.tail() >= frames.front()->data->length) {
                    auto length = static_cast<uint32_t>(frames.front()->data->length);
                    audioBuffer_.writeExact(frames.front()->data->rawData, length);
                    frames.pop_front();
                }
                auto secondReadSize = audioBuffer_.read(data + tSize, size - tSize);
                tSize += secondReadSize;
            }
            renderedDataLength_ += tSize;
        });

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
    frames_.withLock([&](auto& frames){
        frames.push_back(frame);
        while (!frames.empty() && audioBuffer_.tail() >= frames.front()->data->length) {
            auto length = static_cast<uint32_t>(frames.front()->data->length);
            audioBuffer_.writeExact(frames.front()->data->rawData, length);
            if (renderedDataLength_ == 0) {
                renderedDataLength_ = audioInfo_->timePoint2DataLen(static_cast<uint64_t>(frames.front()->ptsTime() * Time::kMicroSecondScale));
                LogI("[seek info]audio render set pos:{}, time:{}", renderedDataLength_, frames.front()->ptsTime());
            }
            frames.pop_front();
        }
    });
}

void AudioRenderComponent::reset() noexcept {
    frames_.withLock([this](auto& frames) {
        audioBuffer_.reset();
        frames.clear();
        renderedDataLength_ = 0;
    });
    clock_.reset();
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
    clock_.start();
}

void AudioRenderComponent::pause() noexcept {
    if (auto pimpl = pimpl_.load()) {
        pimpl->pause();
    } else {
        LogE("audio render is nullptr.");
    }
    clock_.pause();
}

void AudioRenderComponent::stop() noexcept {
    if (auto pimpl = pimpl_.load()) {
        pimpl->stop();
    } else {
        LogE("audio render is nullptr.");
    }
    clock_.reset();
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

void AudioRenderComponent::seek(long double time) noexcept {
    flush();
    audioBuffer_.reset();
    renderedDataLength_ = 0;
    clock_.setTime(Time::TimePoint(time));
}

}

