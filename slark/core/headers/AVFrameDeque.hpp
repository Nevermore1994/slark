//
// Created by Nevermore on 2022/7/11.
// slark AVFrameDeque
// Copyright (c) 2022 Nevermore All rights reserved.
//

#pragma once

#include <deque>
#include <mutex>
#include <memory>
#include "AVFrame.hpp"
#include "NonCopyable.hpp"

namespace slark{

class AVFrameSafeDeque:public NonCopyable{
public:
    AVFrameSafeDeque() = default;
    ~AVFrameSafeDeque() override = default;
    
    void push(AVFramePtr frame);
    void push(AVFrameList& frameList);
    AVFrame* front();
    void pop();
    void swap(AVFrameSafeDeque& deque);
    void clear();
private:
    std::mutex mutex_;
    std::deque<AVFramePtr> frames_;
};

}
