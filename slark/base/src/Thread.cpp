//
// Created by Nevermore on 2021/10/22.
// Slark Thread
// Copyright (c) 2021 Nevermore All rights reserved.
//

#ifndef __APPLE__
    #include <pthread.h>
#endif

#include "Thread.hpp"
#include "Log.hpp"
#include <cassert>

namespace Slark {
using namespace std::chrono;

Thread::~Thread() noexcept {
    stop();
    if (worker_.joinable()) {
        worker_.join();
    }
}

void Thread::stop() noexcept {
    {
        std::unique_lock<std::mutex> lock(mutex_);
        if (isExit_) {
            return;
        }
        isExit_ = true;
        isRunning_ = false;
    }
    cond_.notify_all();
}

void Thread::pause() noexcept {
    std::unique_lock<std::mutex> lock(mutex_);
    if (!isRunning_) {
        return;
    }
    isRunning_ = false;
}

void Thread::resume() noexcept {
    start();
}

void Thread::start() noexcept {
    {
        std::unique_lock<std::mutex> lock(mutex_);
        if (isRunning_ || isExit_) {
            return;
        }
        isRunning_ = true;
    }
    cond_.notify_all();
}

void Thread::process() noexcept {
    if (!isSetting_) {
        setThreadName();
    }
    while (!isExit_) {
        if (isRunning_) {
            lastRunTimeStamp_ = Time::nowTimeStamp();
            if (func_) {
                func_();
            }
            timerPool_.loop();
        } else {
            std::unique_lock<std::mutex> lock(mutex_);
            cond_.wait(lock, [&] {
                return isExit_ || isRunning_;
            });
        }
    }
}


void Thread::setThreadName() noexcept {
#ifndef __APPLE__
    auto handle = worker_.native_handle();
    std::string name = name_;
    if (name.length() > 15) {
        name = name.substr(0, 15);
    }
    pthread_setname_np(handle, name.c_str());
#else
    pthread_setname_np(name_.c_str());
#endif
    isSetting_ = true;
}

TimerId Thread::runAt(uint64_t timeStamp, TimerCallback func) noexcept {
    return timerPool_.runAt(timeStamp, std::move(func));
}

TimerId Thread::runAfter(uint64_t delayTime, TimerCallback func) noexcept {
    return timerPool_.runAfter(delayTime, std::move(func));
}

TimerId Thread::runAfter(milliseconds delayTime, TimerCallback func) noexcept {
    return timerPool_.runAfter(delayTime, std::move(func));
}

}//end namespace Slark
