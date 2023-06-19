//
//  AudioComponent.cpp
//  slark
//
//  Created by Nevermore on 2022/8/19.
//

#include "AudioComponent.hpp"

namespace Slark::Audio {

using namespace Slark;

AudioRenderComponent::AudioRenderComponent(AudioInfo info) {

}

AudioRenderComponent::~AudioRenderComponent() {
    frames_.clear();
}

bool AudioRenderComponent::push(AVFramePtr frame) {
    auto res = true;
    frame->prepareRenderStamp = Time::nowTimeStamp();
    //plugin process data
    if (process) {
        res = process(frame->data);
    }

    frame->pushRenderQueueStamp = Time::nowTimeStamp();
    if (res) {
        push(std::move(frame));
    }

    return res;
}

AVFramePtr AudioRenderComponent::pop() {
    return frames_.pop();
}

}
