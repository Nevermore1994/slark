//
//  AudioRenderNode.m
//  slark
//
//  Created by Nevermore on 2022/8/18.
//

#include "AudioRenderNode.h"
#include "Time.hpp"

namespace slark::Audio {

AudioRenderNode::AudioRenderNode(std::unique_ptr<AudioForamt> format)
    :render_(std::make_unique<AudioRender>(std::move(format))) {
    if(render_){
        render_->requestNextAudioData = [this](uint32_t size, double needTime){
            if(frames_.empty()){
                return Data();
            }
            
            auto frame = frames_.pop();
            if(!frame){
                return Data();
            }
            auto data = frame->detachData();
            if(rendCompletion){
                rendCompletion(std::move(frame));
            }
            return data;
        };
    }
}

AudioRenderNode::~AudioRenderNode() {
    if(render_){
        render_->stop();
        render_.reset();
    }
}

void AudioRenderNode::receive(slark::AVFrameRef frame) noexcept {
    frames_.push(std::unique_ptr<AVFrame>(frame));
}

} //end namespace slark::Audio
