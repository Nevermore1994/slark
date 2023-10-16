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

void OutputNode::notifyTargets(std::shared_ptr<AVFrame> frame) noexcept {
    std::erase_if(targets_, [frame](const auto& pair){
        const auto& [hash, ptr] = pair;
        auto isExpired = ptr.expired();
        if (!isExpired) {
            auto p = ptr.lock();
            p->receive(frame);
        }
        return isExpired;
    });
}

void OutputNode::send(std::shared_ptr<AVFrame> frame) noexcept {
    notifyTargets(std::move(frame));
}


void Node::process(std::shared_ptr<AVFrame> /*frame*/) noexcept {

}

void Node::receive(std::shared_ptr<AVFrame> frame) noexcept {
    process(frame);
    send(frame);
}

}//end namespace slark
