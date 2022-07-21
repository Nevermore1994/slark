//
// Created by Nevermore on 2022/7/11.
// slark AVFrameDeque
// Copyright (c) 2022 Nevermore All rights reserved.
//

#include "AVFrameDeque.hpp"

using namespace slark;

void AVFrameSafeDeque::push(AVFramePtr frame) {
    std::unique_lock<std::mutex> lock(mutex_);
    frames_.push_back(std::move(frame));
}

void AVFrameSafeDeque::push(AVFrameList& frameList) {
    if(!frameList.empty()){
        std::unique_lock<std::mutex> lock(mutex_);
        for(auto& it:frameList){
            frames_.push_back(std::move(it));
        }
    }
    
    frameList.clear();
}

AVFrame *AVFrameSafeDeque::front() {
    std::unique_lock<std::mutex> lock(mutex_);
    if(frames_.empty()){
        return nullptr;
    }
    auto& p = frames_.front();
    return p.get();
}

void AVFrameSafeDeque::pop() {
    std::unique_lock<std::mutex> lock(mutex_);
    if(frames_.empty()){
        return;
    }
    frames_.pop_front();
}

void AVFrameSafeDeque::swap(AVFrameSafeDeque& deque) {
    std::unique_lock<std::mutex> lock(mutex_);
    frames_.swap(deque.frames_);
}

void AVFrameSafeDeque::clear() {
    std::unique_lock<std::mutex> lock(mutex_);
    frames_.clear();
}




