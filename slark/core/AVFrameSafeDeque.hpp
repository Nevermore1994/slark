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
#include "NonCopyable.h"

namespace slark {

class AVFrameSafeDeque : public NonCopyable {
public:
    AVFrameSafeDeque() = default;
    ~AVFrameSafeDeque() override = default;

    void push(AVFramePtr frame) noexcept;
    void push(AVFrameArray& frameList) noexcept;
    AVFramePtr pop() noexcept;
    void swap(AVFrameSafeDeque& deque) noexcept;
    AVFrameArray detachData() noexcept;
    void clear() noexcept;

    inline bool empty() const noexcept {
        return frames_.empty();
    }
private:
    std::mutex mutex_;
    std::deque<AVFramePtr> frames_;
};

}
