//
// Created by Nevermore on 2021/10/25.
// slark ThreadManager
// Copyright (c) 2021 Nevermore All rights reserved.
//
#include "ThreadManager.h"

#include "Log.hpp"

namespace slark {
using namespace std::chrono_literals;

ThreadManager::ThreadManager()
    : monitorThread_([this](std::stop_token token) {
          monitorLoop(token);
      }) {}

ThreadManager::~ThreadManager() {
    monitorThread_.request_stop();
    cond_.notify_all();
    // jthread 析构时自动 join
    {
        std::unique_lock<std::mutex> lock(mutex_);
        threadInfos_.clear();
    }
}

void ThreadManager::monitorLoop(std::stop_token stopToken) {
    while (!stopToken.stop_requested()) {
        {
            std::unique_lock lock(mutex_);
            // 等待变化通知（由 notify_one 唤醒）或 10s 超时
            cond_.wait_for(lock, stopToken, 10s, [] {
                return false;
            });
        }
        if (stopToken.stop_requested()) {
            break;
        }
        reportRunInfo();
    }
}

void ThreadManager::add(const std::shared_ptr<Thread>& thread) noexcept {
    {
        std::unique_lock<std::mutex> lock(mutex_);
        threadInfos_.emplace(thread->getId(), thread);
    }
    cond_.notify_one();
}

void ThreadManager::remove(const std::shared_ptr<Thread>& thread) noexcept {
    {
        std::unique_lock<std::mutex> lock(mutex_);
        threadInfos_.erase(thread->getId());
    }
    cond_.notify_one();
}

void ThreadManager::remove(std::thread::id id) noexcept {
    {
        std::unique_lock<std::mutex> lock(mutex_);
        threadInfos_.erase(id);
    }
    cond_.notify_one();
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
    std::unique_lock<std::mutex> lock(mutex_);
    LogI("ThreadManager report now:{}, now live size :{}", now.point(), threadInfos_.size());
    std::erase_if(threadInfos_, [now](auto& pair) {
        auto& [id, ptr] = pair;
        auto isExpired = ptr.expired();
        if (!isExpired) {
            auto thread = ptr.lock();
            auto interval = now - thread->getLastRunTimeStamp();
            if (thread->isRunning() && interval >= kMaxThreadBlockTimeInterval) {
                LogE("ThreadManager report [{}] is blocking.", std::string(thread->getName()));
            }
        }
        return isExpired;
    });
}

} // end namespace slark
