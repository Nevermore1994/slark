//
// Created by Nevermore on 2021/10/22.
// slark TimerManager
// Copyright (c) 2021 Nevermore All rights reserved.
//
#pragma once

#include <functional>
#include <memory>
#include "Thread.h"
#include "Timer.h"
#include "TimerPool.h"
#include "NonCopyable.h"

namespace slark {

class TimerManager : public NonCopyable {
public:
    inline static TimerManager& shareInstance() {
        static TimerManager instance;
        return instance;
    }

public:
    TimerManager();

    ~TimerManager() noexcept;

    TimerId runAt(uint64_t timeStamp, TimerCallback func) noexcept; //ms
    TimerId runAfter(uint64_t delayTime, TimerCallback func) noexcept; //ms
    TimerId runLoop(uint64_t timeInterval, TimerCallback func) noexcept; //ms
    TimerId runAfter(std::chrono::milliseconds timeStamp, TimerCallback func) noexcept;

    TimerId runLoop(std::chrono::milliseconds timeStamp, TimerCallback func) noexcept;

    void cancel(TimerId id) noexcept;

private:
    void loop();

private:
    std::unique_ptr<Thread> timerThread_;
    TimerPool pool_;
};

}
