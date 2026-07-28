//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#include "ThreadManager.h"

#include <vector>

#include "Log.hpp"

namespace slark {
using namespace std::chrono_literals;

ThreadManager::ThreadManager()
    : monitorThread_([this] {
          monitorLoop();
      }) {}

ThreadManager::~ThreadManager() {
    stopRequested_.store(true);
    cond_.notify_all();
    if (monitorThread_.joinable()) {
        monitorThread_.join();
    }
    {
        std::unique_lock<std::mutex> lock(mutex_);
        threadInfos_.clear();
    }
}

void ThreadManager::monitorLoop() {
    while (!stopRequested_.load()) {
        {
            std::unique_lock lock(mutex_);
            cond_.wait_for(lock, 10s, [this] {
                return stopRequested_.load();
            });
        }
        if (stopRequested_.load()) {
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

void ThreadManager::setBlockCallback(BlockCallback callback) noexcept {
    std::unique_lock<std::mutex> lock(mutex_);
    blockCallback_ = std::move(callback);
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
    std::vector<std::pair<std::string, Time::TimeDelta>> blockedThreads;
    BlockCallback callback;
    size_t liveSize = 0;
    {
        std::unique_lock<std::mutex> lock(mutex_);
        std::erase_if(threadInfos_, [now, &blockedThreads](auto& pair) {
            auto thread = pair.second.lock();
            if (!thread) {
                return true;
            }
            // 仅对活跃中线程做卡死判定，阻塞等待期间 active=false 自然跳过
            if (thread->isRunning() && thread->isActive()) {
                auto interval = now - thread->getLastRunTimeStamp();
                if (interval >= kMaxThreadBlockTimeInterval) {
                    blockedThreads.emplace_back(thread->getName(), interval);
                }
            }
            return false;
        });
        callback = blockCallback_;
        liveSize = threadInfos_.size();
    }
    LogI("ThreadManager report now:{}, live size:{}", now.point(), liveSize);
    if (!callback) {
        return;
    }
    for (auto& [name, duration] : blockedThreads) {
        LogE("ThreadManager report [{}] is blocking.", name);
        callback(name, duration);
    }
}

}  // end namespace slark
