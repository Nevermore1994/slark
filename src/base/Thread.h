//
// Created by Nevermore on 2021/10/22.
// slark Thread
// Copyright (c) 2021 Nevermore All rights reserved.
//
#pragma once

#include <chrono>
#include <cstdio>
#include <shared_mutex>
#include <condition_variable>
#include <print>
#include <thread>
#include <functional>
#include <atomic>
#include <string_view>
#include <type_traits>
#include "TimerPool.h"
#include "Random.hpp"
#include "NonCopyable.h"

namespace slark {

class Thread : public NonCopyable {

public:
    template <class Func, typename ... Args, typename = std::enable_if_t<!std::is_same_v<std::decay_t<Func>, std::thread>>>
    Thread(std::string name, Func&& f, Args&& ... args)
        : name_(std::move(name)), mutex_{}, lastRunTimeStamp_(0)
        , worker_(&Thread::process, this)
        , func_(std::bind(std::forward<Func>(f), std::forward<Args>(args)...)) {
        printf("Thread create %s \n", name_.c_str());
    }

    ~Thread() noexcept override;

    void start() noexcept;

    void pause() noexcept;

    void resume() noexcept;

    void stop() noexcept;

    TimerId runAt(Time::TimePoint timePoint, TimerTask func) noexcept;
    
    TimerId runAfter(std::chrono::milliseconds delayTime, TimerTask func) noexcept;

    TimerId runLoop(std::chrono::milliseconds timeInterval, TimerTask func) noexcept;
public:
    [[nodiscard]] inline bool isRunning() noexcept {
        std::shared_lock<std::shared_mutex> lock(mutex_);
        return isRunning_;
    }

    [[nodiscard]] inline bool isExit() noexcept {
        std::shared_lock<std::shared_mutex> lock(mutex_);
        return isExit_;
    }

    [[nodiscard]] inline Time::TimePoint getLastRunTimeStamp() const noexcept {
        return lastRunTimeStamp_.load();
    }

    [[nodiscard]] inline std::thread::id getId() const noexcept {
        return worker_.get_id();
    }

    [[nodiscard]] inline uint32_t getHashId() const noexcept {
        return static_cast<uint32_t>(std::hash<std::thread::id>{}(worker_.get_id()));
    }

    [[nodiscard]] inline std::string_view getName() const noexcept {
        return std::string_view(name_);
    }

    void setInterval(std::chrono::milliseconds ms) noexcept {
        std::lock_guard<std::shared_mutex> lock(mutex_);
        interval_ = ms;
    }

    std::chrono::milliseconds interval () noexcept {
        std::shared_lock<std::shared_mutex> lock(mutex_);
        return interval_;
    }

private:
    void process() noexcept;

    void setup() noexcept;

private:
    bool isRunning_ = false;
    bool isExit_ = false;
    bool isInit_ = false;
    std::chrono::milliseconds interval_{0};
    std::string name_;
    std::shared_mutex mutex_;
    std::condition_variable_any cond_;
    std::atomic<uint64_t> lastRunTimeStamp_;
    std::thread worker_;
    TimerPool timerPool_;
    std::function<void()> func_;
};

}//end namespace slark

