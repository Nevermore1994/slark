//
// Created by Nevermore on 2021/10/22.
// Slark TimerManager
// Copyright (c) 2021 Nevermore All rights reserved.
//
#pragma once

#include <functional>
#include <memory>
#include "Thread.hpp"
#include "Timer.hpp"
#include "TimerPool.hpp"
#include "NonCopyable.hpp"

namespace Slark {

class TimerManager : public NonCopyable {
public:
    inline static TimerManager& shareInstance() {
        static TimerManager instance;
        return instance;
    }

public:
    TimerManager();

    ~TimerManager() noexcept;

    TimerId runAt(uint64_t timeStamp, TimerCallback func); //ms
    TimerId runAfter(uint64_t delayTime, TimerCallback func); //ms
    TimerId runLoop(uint64_t timeInterval, TimerCallback func); //ms
    TimerId runAfter(std::chrono::milliseconds timeStamp, TimerCallback func) noexcept;

    TimerId runLoop(std::chrono::milliseconds timeStamp, TimerCallback func) noexcept;

    void cancel(TimerId id);

private:
    void loop();

private:
    std::unique_ptr<Thread> timerThread_;
    TimerPool pool_;
};

}
