//
// Created by Nevermore on 2022/5/27.
// slark IOManager
// Copyright (c) 2022 Nevermore All rights reserved.
//
#pragma once

#include "TransportEvent.hpp"
#include "FileHandler.hpp"
//network handler

namespace slark{

#define CallFunc(pointer, func)                                             \
inline void func(){                                                         \
    std::unique_lock<std::mutex> lock(mutex_);                              \
    if (pointer){                                                           \
        pointer->func();                                                    \
    }                                                                       \
}                                                                           \

class IOManager:public slark::NonCopyable, public ITransportObserver{
public:
    IOManager(std::vector<std::string> paths, int16_t index = 0, IOHandlerCallBack func = nullptr);
    ~IOManager() override;
    
    bool open();
    CallFunc(handler_, close);
    CallFunc(handler_, pause);
    CallFunc(handler_, resume);
    
    bool setIndex(int16_t index) noexcept;
    
    void updateEvent(TransportEvent event) override;
    
    inline int16_t index() noexcept {
        return index_;
    }
    
    inline IOState state() noexcept {
        std::unique_lock<std::mutex> lock(mutex_);
        return state_;
    }
    
    std::string_view currentPath() noexcept {
        std::unique_lock<std::mutex> lock(mutex_);
        return handler_ == nullptr ? std::string_view() : handler_->path();
    }
    
private:
    void buildHandler(std::string path);
    void handleData(std::unique_ptr<Data> data, int64_t offset, IOState state);
    bool nextTask() noexcept;
private:
    std::mutex mutex_;
    IOState state_;
    std::atomic<int16_t> index_ = kInvalid;
    uint64_t offset_ = 0;
    std::vector<std::string> paths_;
    std::unique_ptr<IOHandler> handler_;
    IOHandlerCallBack callBack_;
};

#undef CallFunc
}