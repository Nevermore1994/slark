//
// Created by Nevermore on 2023/8/11.
// slark AudioRenderComponentImpl
// Copyright (c) 2023 Nevermore All rights reserved.
//
#include "AudioRenderComponentImpl.h"

namespace slark::Audio {

AudioRenderComponent::AudioRenderComponentImpl::AudioRenderComponentImpl(std::unique_ptr<slark::AudioInfo> info)
    : info_(std::move(info)) {

}

AudioRenderComponent::AudioRenderComponentImpl::~AudioRenderComponentImpl() {

}

void AudioRenderComponent::AudioRenderComponentImpl::receive(slark::AVFrameRefPtr frame) noexcept {

}

}

