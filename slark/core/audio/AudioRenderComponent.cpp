//
// Created by Nevermore on 2023/8/11.
// slark AudioRenderComponentImpl
// Copyright (c) 2023 Nevermore All rights reserved.
//
#include "AudioRenderComponent.h"

namespace slark::Audio {

AudioRenderComponent::AudioRenderComponent(std::shared_ptr<slark::Audio::AudioInfo> info)
    : audioInfo_(info)
    , pimpl_(createAudioRender(info)) {
}

void AudioRenderComponent::receive(AVFrameRefPtr frame) noexcept {
    process(frame);
}

void AudioRenderComponent::process(AVFrameRefPtr frame) noexcept {
    //render
    frames_.push(frame);
    //pimpl_->receive(frame);
    //if render complete, call back
    if (completion) {
        completion(frame);
    }
}

void AudioRenderComponent::clear() noexcept {
    frames_.clear();
}

void AudioRenderComponent::reset() noexcept {
    frames_.clear();
    pimpl_ = createAudioRender(audioInfo_);
}

const std::shared_ptr<AudioInfo> AudioRenderComponent::audioInfo() const noexcept {
    SAssert(audioInfo_ != nullptr, "audio render info is nullptr");
    return audioInfo_;
}

}

