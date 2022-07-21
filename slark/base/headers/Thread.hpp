//
// Created by Nevermore on 2021/10/22.
// slark Thread
// Copyright (c) 2021 Nevermore All rights reserved.
//
#pragma once

#include <mutex>
#include <condition_variable>
#include <thread>
#include <string>
#include <functional>
#include <atomic>
#include <string_view>
#include "TimerPool.hpp"
#include "Utility.hpp"
#include "NonCopyable.hpp"

namespace slark {

class Thread : public NonCopyable {

public:
    template<class Func, typename ... Args>
    Thread(std::string&& name, Func&& f, Args&& ... args)
        :name_(std::move(name))
        , isRunning_(false)
        , isExit_(false)
        , worker_(&Thread::process, this)
        , lastRunTimeStamp_(0) {
        std::unique_lock<std::mutex> lock(mutex_);
        func_ = std::bind(std::forward<Func>(f), std::forward<Args>(args)...);
    }
    
    template<class Func, typename ... Args>
    Thread(const std::string& name, Func&& f, Args&& ... args)
        :name_(name)
        , isRunning_(false)
        , isExit_(false)
        , worker_(&Thread::process, this)
        , lastRunTimeStamp_(0) {
        std::unique_lock<std::mutex> lock(mutex_);
        func_ = std::bind(std::forward<Func>(f), std::forward<Args>(args)...);
    }
    
    explicit Thread(std::string&& name);
    
    explicit Thread(const std::string& name);
    
    ~Thread()  noexcept override;
    
    void start() noexcept;
    
    void pause() noexcept;
    
    void resume() noexcept;
    
    void stop() noexcept;
    
    TimerId runAt(uint64_t timeStamp, TimerCallback func) noexcept;
    
    TimerId runAfter(uint64_t delayTime, TimerCallback func) noexcept; //ms
    TimerId runAfter(std::chrono::milliseconds delayTime, TimerCallback func) noexcept;
    
    template<class Func, typename ... Args>
    void setFunc(Func&& f, Args&& ... args) noexcept {
        std::unique_lock<std::mutex> lock(mutex_);
        func_ = std::bind(std::forward<Func>(f), std::forward<Args>(args)...);
    }

public:
    inline bool isRunning() const noexcept {
        return isRunning_.load();
    }
    
    inline Time::TimeStamp getLastRunTimeStamp() const noexcept {
        return lastRunTimeStamp_;
    }
    
    inline std::thread::id getId() const noexcept {
        return worker_.get_id();
    }
    
    inline std::string_view getName() const noexcept {
        return std::string_view(name_);
    }

private:
    void process() noexcept;
    
    void setThreadName() noexcept;

private:
    std::function<void()> func_;
    std::atomic<bool> isRunning_;
    std::atomic<bool> isExit_;
    std::string name_;
    std::mutex mutex_;
    std::condition_variable cond_;
    Time::TimeStamp lastRunTimeStamp_ = 0;
    std::thread worker_;
    TimerPool timerPool_;
};

}//end namespace slark

