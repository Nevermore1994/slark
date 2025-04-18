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
    
    ~DecoderComponent() override;

    bool open(DecoderType type, const std::shared_ptr<DecoderConfig>& config) noexcept;
    
    void close() noexcept;
    
    void send(AVFramePtr packet) noexcept;
    
    void flush() noexcept;
    
    void start() noexcept;
    
    void pause() noexcept;

    bool empty() noexcept {
        std::lock_guard lock(mutex_);
        return pendingDecodeQueue_.empty();
    }

    AVFrameRefPtr requestDecodeFrame(bool isBlocking) noexcept override;
private:
    void pushFrameDecode();

    AVFrameRefPtr peekDecodeFrame() noexcept;
private:
    std::atomic_bool isOpened_ = false;
    DecoderReceiveFunc callback_;
    Synchronized<std::shared_ptr<IDecoder>> decoder_;
    Thread decodeWorker_;
    std::mutex mutex_;
    std::deque<AVFrameRefPtr> pendingDecodeQueue_;
    std::condition_variable cond_;
};

}
