//
// Created by Nevermore on 2021/10/22.
// Slark Thread
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
#include "Random.hpp"
#include "NonCopyable.hpp"

namespace Slark {

class Thread : public NonCopyable {

public:
    template<class Func, typename ... Args>
    Thread(std::string&& name, Func&& f, Args&& ... args)
        : name_(std::move(name))
        ,lastRunTimeStamp_(0)
        , worker_(&Thread::process, this) {
        func_ = std::bind(std::forward<Func>(f), std::forward<Args>(args)...);
    }

    template<class Func, typename ... Args>
    Thread(const std::string& name, Func&& f, Args&& ... args)
        : name_(name)
        , lastRunTimeStamp_(0)
        , worker_(&Thread::process, this) {
        func_ = std::bind(std::forward<Func>(f), std::forward<Args>(args)...);
    }

    ~Thread() noexcept override;

    void start() noexcept;

    void pause() noexcept;

    void resume() noexcept;

    void stop() noexcept;

    TimerId runAt(uint64_t timeStamp, TimerCallback func) noexcept;
    TimerId runAfter(uint64_t delayTime, TimerCallback func) noexcept; //ms
    TimerId runAfter(std::chrono::milliseconds delayTime, TimerCallback func) noexcept;

public:
    inline bool isRunning() noexcept {
        std::unique_lock<std::mutex> lock(mutex_);
        return isRunning_;
    }

    [[nodiscard]] inline Time::Timestamp getLastRunTimeStamp() const noexcept {
        return lastRunTimeStamp_;
    }

    [[nodiscard]] inline std::thread::id getId() const noexcept {
        return worker_.get_id();
    }

    [[nodiscard]] inline std::string_view getName() const noexcept {
        return std::string_view(name_);
    }

private:
    void process() noexcept;

    void setThreadName() noexcept;

private:
    std::function<void()> func_;
    bool isRunning_ = false;
    bool isExit_ = false;
    bool isSetting_ = false;
    std::string name_;
    std::mutex mutex_;
    std::condition_variable cond_;
    Time::Timestamp lastRunTimeStamp_ = 0;
    std::thread worker_;
    TimerPool timerPool_;
};

}//end namespace Slark

