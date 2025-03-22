//
// Created by Nevermore on 2023/7/19.
// slark DecoderComponent
// Copyright (c) 2023 Nevermore All rights reserved.
//
#pragma once
#include "DecoderManager.h"
#include "Thread.h"
#include "Synchronized.hpp"

namespace slark {

class DecoderComponent : public DecoderDataProvider,
        public std::enable_shared_from_this<DecoderComponent> {
public:
    explicit DecoderComponent(DecoderReceiveFunc&& callback);
    
    ~DecoderComponent();

    bool open(DecoderType type, const std::shared_ptr<DecoderConfig>& config) noexcept;
    
    void close() noexcept;
    
    void send(AVFramePtr packet) noexcept;
    
    void flush() noexcept;
    
    void start() noexcept;
    
    void pause() noexcept;

    bool isReachEnd() const noexcept {
        return isReachEnd_;
    }

    void setReachEnd(bool isReachEnd) noexcept{
        isReachEnd_ = isReachEnd;
    }

    bool empty() noexcept {
        std::lock_guard lock(mutex_);
        return pendingDecodeQueue_.empty();
    }

private:
    void pushFrameDecode();
private:
    std::atomic_bool isReachEnd_ = false;
    DecoderReceiveFunc callback_;
    Synchronized<std::shared_ptr<IDecoder>> decoder_;
    Synchronized<std::unique_ptr<Thread>> decodeWorker_;
    std::mutex mutex_;
    std::deque<AVFramePtr> pendingDecodeQueue_;
    std::condition_variable cond_;
};

}
