//
//  AudioNode.hpp
//  slark
//
//  Created by 谭平 on 2022/8/24.
//

#pragma once
#include <unordered_map>
#include <memory>
#include "AVFrame.hpp"

namespace slark::Audio {

///AudioNode don't hold AVFrameRef, let AVFrameRef flow freely
///only when node is last section

class IAudioInputNode {
public:
    virtual ~IAudioInputNode() = default;
    virtual void receive(slark::AVFrameRef frame) noexcept = 0;
};

class IAudioOutputNode {
public:
    virtual ~IAudioOutputNode() = default;

    void process() noexcept;
    bool addTarget(std::weak_ptr<IAudioInputNode> node) noexcept;
    bool removeTarget(std::weak_ptr<IAudioInputNode> node) noexcept;
    void removeAllTarget() noexcept;
protected:
    void notifyTargets(slark::AVFrameRef frame) noexcept;
    virtual slark::AVFrameRef send() noexcept = 0;
private:
    std::unordered_map<uint64_t, std::weak_ptr<IAudioInputNode>> targets_;
};
}
