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
    std::lock_guard lock(mutex_);
    threadInfos_.clear();
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
    if (!thread || thread->hasExited()) {
        return;
    }
    {
        std::lock_guard lock(mutex_);
        threadInfos_.insert_or_assign(thread->serial(), thread);
    }
    cond_.notify_one();
}

void ThreadManager::remove(const std::shared_ptr<Thread>& thread) noexcept {
    if (!thread) {
        return;
    }
    {
        std::lock_guard lock(mutex_);
        threadInfos_.erase(thread->serial());
    }
    cond_.notify_one();
}

size_t ThreadManager::size() const noexcept {
    std::lock_guard lock(mutex_);
    return threadInfos_.size();
}

void ThreadManager::setBlockCallback(BlockCallback callback) noexcept {
    std::lock_guard lock(mutex_);
    blockCallback_ = std::move(callback);
}

void ThreadManager::reportRunInfo() noexcept {
    const auto now = Time::nowTimeStamp();
    std::vector<std::shared_ptr<Thread>> threads;
    std::vector<std::pair<std::string, Time::TimeDelta>> blockedThreads;
    BlockCallback callback;
    size_t liveSize = 0;
    {
        std::lock_guard lock(mutex_);
        for (auto it = threadInfos_.begin(); it != threadInfos_.end();) {
            if (auto thread = it->second.lock()) {
                threads.push_back(std::move(thread));
                ++it;
            } else {
                it = threadInfos_.erase(it);
            }
        }
        callback = blockCallback_;
        liveSize = threadInfos_.size();
    }

    for (const auto& thread : threads) {
        if (thread->isRunning() && thread->isActive()) {
            const auto interval = now - thread->getLastRunTimeStamp();
            if (interval >= kMaxThreadBlockTimeInterval) {
                blockedThreads.emplace_back(thread->getName(), interval);
            }
        }
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

}  // namespace slark
