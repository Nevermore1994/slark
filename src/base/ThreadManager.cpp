//
// Created by Nevermore on 2021/10/25.
// slark ThreadManager
// Copyright (c) 2021 Nevermore All rights reserved.
//
#include "ThreadManager.h"
#include "Log.hpp"
#include "TimerManager.h"

namespace slark {
using namespace slark::Random;
using namespace std::chrono_literals;

ThreadManager::ThreadManager() {
    timerId_ = TimerManager::shareInstance().runLoop(10s, [this]() {
        reportRunInfo();
    });
}

ThreadManager::~ThreadManager() {
    {
        std::unique_lock<std::mutex> lock(mutex_);
        threadInfos_.clear();
    }
    TimerManager::shareInstance().cancel(timerId_);
}


void ThreadManager::add(const std::shared_ptr<Thread>& thread) noexcept {
    std::unique_lock<std::mutex> lock(mutex_);
    threadInfos_.emplace(thread->getId(), thread);
}

void ThreadManager::remove(const std::shared_ptr<Thread>& thread) noexcept {
    std::unique_lock<std::mutex> lock(mutex_);
    threadInfos_.erase(thread->getId());
}

void ThreadManager::remove(std::thread::id id) noexcept {
    std::unique_lock<std::mutex> lock(mutex_);
    threadInfos_.erase(id);
}

std::shared_ptr<Thread> ThreadManager::thisThread() noexcept {
    std::unique_lock<std::mutex> lock(mutex_);
    auto id = std::this_thread::get_id();
    auto it = threadInfos_.find(id);
    if (it == threadInfos_.end() || it->second.expired()) {
        return nullptr;
    }
    return it->second.lock();
}

void ThreadManager::reportRunInfo() noexcept {
    Time::TimePoint now = Time::nowTimeStamp();
    LogI("ThreadManager report now:{}, now live size :{}", now.count, threadInfos_.size());
    std::unique_lock<std::mutex> lock(mutex_);
    std::erase_if(threadInfos_, [now] (auto& pair) {
        auto& [id, ptr] = pair;
        auto isExpired = ptr.expired();
        if (!isExpired) {
            auto thread = ptr.lock();
            Time::TimePoint interval = (now - thread->getLastRunTimeStamp());
            if (thread->isRunning() && interval >= kMaxThreadBlockTimeInterval) {
                LogE("ThreadManager report [{}] is blocking.", thread->getName().data());
            }
        }
        return isExpired;
    });
}

}//end namespace slark
