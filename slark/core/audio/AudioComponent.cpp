//
//  AudioComponent.cpp
//  slark
//
//  Created by Nevermore on 2022/8/19.
//

#include "AudioComponent.h"
#include "AudioRenderComponentImpl.h"
#include "AudioRecorderComponentImpl.h"

namespace slark::Audio {

using namespace slark;

AudioRenderComponent::AudioRenderComponent(std::unique_ptr<AudioInfo> info)
    : pimpl_(std::make_unique<AudioRenderComponentImpl>(std::move(info))) {

}

void AudioRenderComponent::receive(std::shared_ptr<AVFrame> frame) noexcept {
    process(frame);
}

void AudioRenderComponent::process(std::shared_ptr<AVFrame> frame) noexcept {
    //render
    pimpl_->receive(frame);
    //if render complete, call back
    if (completion) {
        completion(frame);
    }
}

void AudioRecorderComponent::process(std::shared_ptr<AVFrame> frame) noexcept {
    OutputNode::send(frame);
}

void AudioRecorderComponent::send(AVFrameRefPtr frame) noexcept {
    process(std::move(frame));
}


}
