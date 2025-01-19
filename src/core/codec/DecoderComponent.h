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

class DecoderComponent : public NonCopyable {
public:
    explicit DecoderComponent(DecoderReceiveFunc&& callback);
    ~DecoderComponent() override;

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
        packets_.withReadLock([&isNeed](auto& packets){
            isNeed = packets.empty();
        });
        return isNeed;
    }

private:
    void decode();
private:
    std::atomic<bool> isReachEnd_ = false;
    std::mutex mutex_;
    std::unique_ptr<IDecoder> decoder_;
    DecoderReceiveFunc callback_;
    Thread decodeWorker_;
    Synchronized<std::deque<AVFramePtr>, std::shared_mutex> packets_;
};

}
