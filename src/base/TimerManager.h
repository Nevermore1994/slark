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
    static TimerManager& shareInstance();

public:
    TimerManager();

    ~TimerManager() final;

    TimerId runAt(Time::TimePoint timePoint,
                  TimerTask func,
                  ExecuteMode mode = ExecuteMode::Serial) noexcept;

    TimerId runAfter(std::chrono::milliseconds timeStamp,
                     TimerTask func,
                     ExecuteMode mode = ExecuteMode::Serial) noexcept;

    TimerId runLoop(std::chrono::milliseconds timeStamp,
                    TimerTask func,
                    ExecuteMode mode = ExecuteMode::Serial) noexcept;

    void cancel(TimerId id) noexcept;

    void clear() noexcept;

private:
    void loop();

private:
    bool isExited_ = false;
    std::mutex mutex_;
    std::condition_variable cond_;
    std::unique_ptr<Thread> timerThread_;
    TimerPool pool_;
};

}
