//
// Created by Nevermore on 2021/10/22.
// slark TimerManager
// Copyright (c) 2021 Nevermore All rights reserved.
//
#pragma once

#include "NonCopyable.h"
#include "Thread.h"
#include "Timer.h"
#include "TimerPool.h"

namespace slark {

class TimerManager final : public NonCopyable {
public:
    inline static TimerManager& shareInstance() {
        static TimerManager instance;
        return instance;
    }

public:
    TimerManager();

    ~TimerManager() final;

    TimerId runAt(Time::TimePoint timePoint, TimerTask func) noexcept;

    TimerId runAfter(std::chrono::milliseconds timeStamp, TimerTask func) noexcept;

    TimerId runLoop(std::chrono::milliseconds timeStamp, TimerTask func) noexcept;

    void cancel(TimerId id) noexcept;

    void clear() noexcept;

private:
    void loop();

private:
    std::unique_ptr<Thread> timerThread_;
    TimerPool pool_;
};

}
