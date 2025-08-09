//
// Created by Nevermore on 2021/10/22.
// slark Thread
// Copyright (c) 2021 Nevermore All rights reserved.
//
#pragma once

#include <chrono>
#include <shared_mutex>
#include <condition_variable>
#include <print>
#include <thread>
#include <functional>
#include <atomic>
#include <string_view>
#include <type_traits>
#include <concepts>
#include "TimerPool.h"
#include "Random.hpp"
#include "NonCopyable.h"

namespace slark {


class Thread : public NonCopyable {

public:
    template <typename Func, typename ... Args>
    requires std::is_invocable_v<Func, Args...>
    Thread(std::string name, Func&& f, Args&& ... args)
        : name_(std::move(name))
        , lastRunTimeStamp_(0)
        , func_(std::bind(std::forward<Func>(f), std::forward<Args>(args)...))
        , worker_(&Thread::process, this) {
        std::println("Thread create:{}", name_);
    }
    
    template <typename Func, typename ... Args>
    requires std::is_invocable_v<Func, Args...>
    Thread(Func&& f, Args&& ... args)
        : lastRunTimeStamp_(0)
        , func_(std::bind(std::forward<Func>(f), std::forward<Args>(args)...))
        , worker_(&Thread::process, this) {
        
    }

    ~Thread() noexcept override;

    void start() noexcept;

    void pause() noexcept;

    void stop() noexcept;

    TimerId runAt(Time::TimePoint timePoint, TimerTask func) noexcept;
    
    TimerId runAfter(std::chrono::milliseconds delayTime, TimerTask func) noexcept;

    TimerId runLoop(std::chrono::milliseconds timeInterval, TimerTask func) noexcept;
public:
    [[nodiscard]] inline bool isRunning() const  noexcept {
        std::shared_lock<std::shared_mutex> lock(mutex_);
        return isRunning_;
    }

    [[nodiscard]] inline bool isExit() const noexcept {
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

    std::chrono::milliseconds interval() const noexcept {
        std::shared_lock<std::shared_mutex> lock(mutex_);
        return interval_;
    }

    void setThreadName(std::string_view nameView) noexcept;
private:
    void process() noexcept;
    
    void setup() noexcept;
private:
    bool isRunning_ = false;
    bool isExit_ = false;
    std::atomic<bool> isInit_ = false;
    std::chrono::milliseconds interval_{0};
    std::string name_;
    mutable std::shared_mutex mutex_;
    std::condition_variable_any cond_;
    std::atomic<uint64_t> lastRunTimeStamp_;
    std::function<void()> func_;
    std::thread worker_;
    TimerPool timerPool_;
};

}//end namespace slark

