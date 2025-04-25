//
//  Node.cpp
//  slark
//
//  Created by Nevermore.
//

#include <functional>
#include <utility>
#include <ranges>
#include "Node.h"
#include "Util.hpp"

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
    for (auto& p : std::views::values(targets_)
        | std::views::filter([](const auto& p) { return !p.expired();})) {
        if (auto ref = p.lock(); ref) {
            ref->send(frame);
        }
    }
    std::erase_if(targets_, [frame](const auto& pair){
        return pair.second.expired();
    });
}

void OutputNode::receive(std::shared_ptr<AVFrame> frame) noexcept {
    notifyTargets(std::move(frame));
}

bool Node::process(std::shared_ptr<AVFrame> /*frame*/) noexcept {
    return true;
}

bool Node::send(std::shared_ptr<AVFrame> frame) noexcept {
    process(frame);
    receive(frame);
    return true;
}

}//end namespace slark
