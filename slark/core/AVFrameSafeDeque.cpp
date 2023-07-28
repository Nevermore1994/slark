//
// Created by Nevermore on 2022/7/11.
// slark AVFrameDeque
// Copyright (c) 2022 Nevermore All rights reserved.
//

#include "AVFrameSafeDeque.hpp"

namespace slark {

void AVFrameSafeDeque::push(AVFramePtr frame) noexcept {
    std::unique_lock<std::mutex> lock(mutex_);
    frames_.push_back(std::move(frame));
}

void AVFrameSafeDeque::push(AVFrameArray& frameList) noexcept {
    if (!frameList.empty()) {
        std::unique_lock<std::mutex> lock(mutex_);
        for (auto& frame : frameList) {
            frames_.push_back(std::move(frame));
        }
    }
    frameList.clear();
}

AVFramePtr AVFrameSafeDeque::pop() noexcept {
    std::unique_lock<std::mutex> lock(mutex_);
    if (frames_.empty()) {
        return nullptr;
    }
    auto ptr = std::move(frames_.front());
    frames_.pop_front();
    return ptr;
}

void AVFrameSafeDeque::swap(AVFrameSafeDeque& deque) noexcept {
    std::unique_lock<std::mutex> lock(mutex_);
    frames_.swap(deque.frames_);
}


void AVFrameSafeDeque::clear() noexcept {
    std::unique_lock<std::mutex> lock(mutex_);
    decltype(frames_) tempQueue;
    frames_.swap(tempQueue);
}

AVFrameArray AVFrameSafeDeque::detachData() noexcept {
    AVFrameArray frameList;
    decltype(frames_) tempDeque;
    if (!empty()){
        std::unique_lock<std::mutex> lock(mutex_);
        tempDeque.swap(frames_);
    }
    while (!tempDeque.empty()) {
        frameList.push_back(std::move(tempDeque.front()));
        tempDeque.pop_front();
    }
    return frameList;
}

}//end namespace slark


