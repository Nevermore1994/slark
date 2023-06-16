//
//  AudioNode.cpp
//  slark
//
//  Created by 谭平 on 2022/8/24.
//

#include <functional>
#include <vector>
#include "AudioNode.hpp"

namespace slark::Audio {

using namespace slark;

void IAudioInputNode::receive(AVFrameRef frame) noexcept {

}

bool IAudioOutputNode::addTarget(std::weak_ptr<IAudioInputNode> node) noexcept {
    if (node.expired()) {
        return false;
    }

    auto hash = reinterpret_cast<uint64_t>(node.lock().get());
    return targets_.insert({hash, std::move(node)}).second;
}

bool IAudioOutputNode::removeTarget(std::weak_ptr<IAudioInputNode> node) noexcept {
    if (node.expired()) {
        return false;
    }

    auto hash = reinterpret_cast<uint64_t>(node.lock().get());
    return targets_.erase(hash) > 0;
}

void IAudioOutputNode::removeAllTarget() noexcept {
    targets_.clear();
}

void IAudioOutputNode::notifyTargets(AVFrameRef frame) noexcept {
    //C++20 replace erase_if
    for (auto it = targets_.begin(); it != targets_.end();) {
        auto& [hash, ptr] = *it;
        if (ptr.expired()) {
            it = targets_.erase(it);
        } else {
            auto p = ptr.lock();
            p->receive(frame);
            ++it;
        }
    }
}

void IAudioOutputNode::process() noexcept {
    auto frame = send();
    notifyTargets(frame);
}

}//end namespace slark
