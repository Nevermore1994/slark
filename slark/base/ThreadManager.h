//
// Created by Nevermore on 2021/10/25.
// slark ThreadManager
// Copyright (c) 2021 Nevermore All rights reserved.
//
#pragma once

#include <thread>
#include <unordered_map>
#include "Thread.h"

namespace slark {

constexpr const Time::TimeStamp kMaxThreadBlockTimeInterval = std::chrono::seconds(30).count(); //30s

class ThreadManager final: public NonCopyable {
public:
    static inline ThreadManager& shareInstance() {
        static ThreadManager instance;
        std::this_thread::get_id();
        return instance;
    }

public:
    ThreadManager();

    ~ThreadManager() final;

    void add(const std::shared_ptr<Thread>& thread) noexcept;

    void remove(const std::shared_ptr<Thread>& thread) noexcept;

    void remove(std::thread::id id) noexcept;

    std::shared_ptr<Thread> thisThread() noexcept;

private:
    void reportRunInfo() noexcept;

private:
    std::unordered_map<std::thread::id, std::weak_ptr<Thread>> threadInfos_;
    std::mutex mutex_;
    TimerId timerId_;
};

}

