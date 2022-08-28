//
// Created by Nevermore on 2021/10/25.
// slark ThreadManager
// Copyright (c) 2021 Nevermore All rights reserved.
//
#pragma once

#include <thread>
#include <unordered_map>
#include "Thread.hpp"

namespace slark {

constexpr const Time::Timestamp kMaxThreadBlockTimeInterval = std::chrono::seconds(30).count(); //30s

class ThreadManager : public NonCopyable {
public:
    static inline ThreadManager& shareInstance() {
        static ThreadManager instance;
        std::this_thread::get_id();
        return instance;
    }

public:
    ThreadManager();
    
    ~ThreadManager();
    
    void add(std::shared_ptr<Thread> thread);
    
    void remove(const std::shared_ptr<Thread>& thread);
    
    void remove(std::thread::id id);
    
    Thread& thisThread();

private:
    void reportRunInfo() noexcept;

private:
    std::unordered_map<std::thread::id, std::weak_ptr<Thread>> threadInfos_;
    std::mutex mutex_;
    TimerId timerId_;
};

}

