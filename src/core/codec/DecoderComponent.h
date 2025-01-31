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
                            std::enable_shared_from_this<DecoderComponent> {
public:
    explicit DecoderComponent(DecoderReceiveFunc&& callback);
    
    ~DecoderComponent() override;
    
    virtual AVFramePtr getDecodeFrame() noexcept override;

    bool open(DecoderType type, std::shared_ptr<DecoderConfig> config) noexcept;
    
    void close() noexcept;
    
    void send(AVFramePtr packet) noexcept;
    
    void flush() noexcept;
    
    void start() noexcept;
    
    void pause() noexcept;
    
    void reset() noexcept;
    
    void setReachEnd(bool isReachEnd) noexcept {
        isReachEnd_ = isReachEnd;
    }
    
    bool isNeedPushFrame() noexcept {
        bool isNeed = true;
        {
            std::lock_guard lock(frameMutex_);
            isNeed = pendingDecodePacketQueue_.empty();
        }
        return isNeed;
    }

private:
    void decode();
private:
    std::atomic<bool> isReachEnd_ = false;
    std::mutex decoderMutex_;
    std::unique_ptr<IDecoder> decoder_;
    std::mutex frameMutex_;
    std::condition_variable cond_;
    std::deque<AVFramePtr> pendingDecodePacketQueue_;
    DecoderReceiveFunc callback_;
    Thread decodeWorker_;
};

}
