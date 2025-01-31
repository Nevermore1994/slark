//
// Created by Nevermore on 2021/10/22.
// slark Thread
// Copyright (c) 2021 Nevermore All rights reserved.
//

#include <thread>

#ifndef __APPLE__
#include <pthread.h>
#endif

#include "Thread.h"
#include "Assert.hpp"

namespace slark {
using std::chrono::milliseconds;
using namespace std::chrono_literals;

Thread::~Thread() noexcept {
    SAssert(worker_.get_id() != std::this_thread::get_id(), "error, release itself in the current thread");
    stop();
    if (worker_.joinable()) {
        worker_.join();
    }
}

void Thread::stop() noexcept {
    {
        std::lock_guard<std::shared_mutex> lock(mutex_);
        if (isExit_) {
            return;
        }
        isExit_ = true;
        isRunning_ = false;
    }
    cond_.notify_all();
}

void Thread::pause() noexcept {
    std::lock_guard<std::shared_mutex> lock(mutex_);
    if (!isRunning_) {
        return;
    }
    isRunning_ = false;
}

void Thread::start() noexcept {
    {
        std::lock_guard<std::shared_mutex> lock(mutex_);
        if (isRunning_ || isExit_) {
            return;
        }
        isRunning_ = true;
    }
    cond_.notify_all();
}

void Thread::process() noexcept {
    while (!isExit_) {
        if (!isInit_) {
            setup();
        }
        if (isRunning_) {
            lastRunTimeStamp_ = Time::nowTimeStamp();
            if (func_) {
                func_();
            }
            timerPool_.loop();
            if (interval_ > 0ms) {
                std::this_thread::sleep_for(interval_);
            }
        } else {
            std::unique_lock<std::shared_mutex> lock(mutex_);
            cond_.wait(lock, [this] {
                return isExit_ || isRunning_;
            });
        }
    }
}

void Thread::setThreadName(std::string_view nameView) noexcept {
    std::lock_guard lock(mutex_);
    name_ = std::string(nameView);
    isInit_ = false;
}

void Thread::setup() noexcept {
#ifndef __APPLE__
    auto handle = worker_.native_handle();
    std::string name = name_;
    name = name.substr(0, 15);   //linux thread name length < 15
    pthread_setname_np(handle, name.c_str());
#else
    pthread_setname_np(name_.c_str());
#endif
    isInit_ = true;
}

TimerId Thread::runAt(Time::TimePoint timeStamp, TimerTask func) noexcept {
    return timerPool_.runAt(timeStamp, std::move(func));
}

TimerId Thread::runAfter(milliseconds delayTime, TimerTask func) noexcept {
    return timerPool_.runAfter(delayTime, std::move(func));
}

TimerId Thread::runLoop(milliseconds timeInterval, TimerTask func) noexcept {
    return timerPool_.runLoop(timeInterval, std::move(func));
}

}//end namespace slark
