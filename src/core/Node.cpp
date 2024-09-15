//
//  Node.cpp
//  slark
//
//  Created by Nevermore.
//

#include <functional>
#include <utility>
#include "Node.h"
#include "Utility.hpp"

namespace slark {

bool OutputNode::addTarget(std::weak_ptr<InputNode> node) noexcept {
    if (auto hash = Util::getWeakPtrAddress(node); hash.has_value()) {
        return targets_.insert({hash.value(), std::move(node)}).second;
    }
    return false;
}

bool OutputNode::removeTarget(const std::weak_ptr<InputNode>& node) noexcept {
    if (auto hash = Util::getWeakPtrAddress(node); hash.has_value()) {
        return targets_.erase(hash.value()) > 0;
    }
    return false;
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
