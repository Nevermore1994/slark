//
//  AudioComponent.cpp
//  slark
//
//  Created by Nevermore on 2022/8/19.
//

#include "AudioComponent.h"

namespace slark::Audio {

using namespace slark;

AudioRenderComponent::AudioRenderComponent(AudioInfo /*info*/)
    : pimpl_(nullptr) {

}

void AudioRenderComponent::receive(std::shared_ptr<AVFrame> frame) noexcept {
    process(frame);
}

void AudioRenderComponent::process(std::shared_ptr<AVFrame> frame) noexcept {
    //render
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
