//
//  AudioRenderNode.h
//  slark
//
//  Created by Nevermore on 2022/8/19.
//

#ifndef AudioRenderDataTransporter_h
#define AudioRenderDataTransporter_h

#include "AudioRender.h"
#include "AudioDefine.h"
#include "AVFrameSafeDeque.hpp"
#include "Node.h"

namespace slark::Audio {

class AudioRenderNode:public IAudioInputNode {
public:
    AudioRenderNode(std::unique_ptr<AudioForamt> format);
    ~AudioRenderNode() override;
    void receive(slark::AVFrameRef frame) noexcept override;
    
    std::function<void(AVFramePtr)> rendCompletion;
    
private:
    std::unique_ptr<AudioRender> render_;
    slark::AVFrameSafeDeque frames_;
};

}

#endif /* AudioRenderDataTransporter_h */
