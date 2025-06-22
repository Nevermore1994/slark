//
// Created by Nevermore on 2024/5/12.
// slark IOBase
// Copyright (c) 2024 Nevermore All rights reserved.
//

#pragma once
#include "IOBase.h"
#include "NonCopyable.h"
#include "Data.hpp"
#include "Range.h"
#include <mutex>

namespace slark {

using ReaderDataCallBack = std::function<void(DataPacket, IOState)>;

enum class ReaderType {
    Local,
    NetWork
};

constexpr uint64_t kReadDefaultSize = 1024 * 64; //64kb

struct ReaderTask {
    std::string path;
    ReaderDataCallBack callBack;
    Range range;
    uint64_t readBlockSize = kReadDefaultSize; //only local
    std::chrono::milliseconds timeInterval{5}; //only local
    
    explicit ReaderTask(ReaderDataCallBack&& func)
        : callBack(std::move(func)) {
        
    }
};

using ReaderTaskPtr = std::unique_ptr<ReaderTask>;

struct IReader: public NonCopyable {
    ~IReader() override = default;
    
    virtual bool open(ReaderTaskPtr) noexcept = 0;
    
    virtual void updateReadRange(Range range) noexcept = 0;
    
    virtual IOState state() noexcept = 0;
    
    virtual void reset() noexcept = 0;
    
    virtual void close() noexcept = 0;
    
    virtual void start() noexcept {};
    
    virtual void pause() noexcept {};
    
    virtual bool isCompleted() noexcept = 0;
    
    virtual bool isRunning() noexcept = 0;
    
    virtual void seek(uint64_t pos) noexcept = 0;
    
    virtual uint64_t size() noexcept = 0;
    
    ReaderType type() const noexcept {
        return type_;
    }
    
    bool isLocal() const noexcept {
        return type_ == ReaderType::Local;
    }
    
    bool isNetwork() const noexcept {
        return type_ == ReaderType::NetWork;
    }
    
protected:
    ReaderType type_;
    std::mutex taskMutex_;
    ReaderTaskPtr task_;
};

}
