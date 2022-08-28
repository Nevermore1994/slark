//
// Created by Nevermore on 2022/7/11.
// slark AVFrameDeque
// Copyright (c) 2022 Nevermore All rights reserved.
//

#include "AVFrameDeque.hpp"

namespace slark {

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

AVFramePtr AVFrameSafeDeque::pop() {
    std::unique_lock<std::mutex> lock(mutex_);
    if(frames_.empty()){
        return nullptr;
    }
    auto ptr = std::move(frames_.front());
    frames_.pop_front();
    return ptr;
}

void AVFrameSafeDeque::swap(AVFrameSafeDeque& deque) {
    std::unique_lock<std::mutex> lock(mutex_);
    frames_.swap(deque.frames_);
}

void AVFrameSafeDeque::clear() {
    std::unique_lock<std::mutex> lock(mutex_);
    frames_.clear();
}

}//end namespace slark


