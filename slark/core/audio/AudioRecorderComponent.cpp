//
//  AudioRecorderComponent.cpp
//  slark
//
//

#include "AudioRecorderComponent.hpp"

namespace slark::Audio {

void AudioRecorderComponent::process(std::shared_ptr<AVFrame> frame) noexcept {
    OutputNode::send(frame);
}

void AudioRecorderComponent::send(AVFrameRefPtr frame) noexcept {
    process(std::move(frame));
}

}
