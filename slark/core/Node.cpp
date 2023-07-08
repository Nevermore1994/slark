//
//  Node.cpp
//  slark
//
//  Created by Nevermore on 2022/8/24.
//

#include <functional>
#include <utility>
#include <vector>
#include "Node.h"

namespace slark {

void InputNode::receive(std::shared_ptr<AVFrame> frame) noexcept {
    frame_ = std::move(frame);
}

void InputNode::process() noexcept {

}

bool OutputNode::addTarget(std::weak_ptr<InputNode> node) noexcept {
    if (node.expired()) {
        return false;
    }

    auto hash = reinterpret_cast<uint64_t>(node.lock().get());
    return targets_.insert({hash, std::move(node)}).second;
}

bool OutputNode::removeTarget(const std::weak_ptr<InputNode>& node) noexcept {
    if (node.expired()) {
        return false;
    }

    auto hash = reinterpret_cast<uint64_t>(node.lock().get());
    return targets_.erase(hash) > 0;
}

void OutputNode::removeAllTarget() noexcept {
    targets_.clear();
}

void OutputNode::notifyTargets() noexcept {
    //C++20 replace erase_if
    for (auto it = targets_.begin(); it != targets_.end();) {
        auto& [hash, ptr] = *it;
        if (ptr.expired()) {
            it = targets_.erase(it);
        } else {
            auto p = ptr.lock();
            p->receive(frame_);
            ++it;
        }
    }
}

void OutputNode::process() noexcept {
    notifyTargets();
}

void Node::process() noexcept {
    OutputNode::process();
}

}//end namespace slark
