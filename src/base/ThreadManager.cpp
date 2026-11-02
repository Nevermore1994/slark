//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#include "ThreadManager.h"

#include <algorithm>
#include <vector>

#include "Log.hpp"

namespace slark {

ThreadManager& ThreadManager::shareInstance() {
    static ThreadManager* instance = new ThreadManager();
    return *instance;
}

ThreadManager::ThreadManager() : ThreadManager(Config{}) {}

ThreadManager::ThreadManager(Config config)
    : config_(config), monitorThread_([this] {
          monitorLoop();
      }) {}

ThreadManager::~ThreadManager() {
    {
        std::lock_guard lock(mutex_);
        stopRequested_ = true;
    }
    cond_.notify_all();
    if (monitorThread_.joinable()) {
        monitorThread_.join();
    }
}

void ThreadManager::monitorLoop() {
    std::unique_lock lock(mutex_);
    while (!stopRequested_) {
        if (threadInfos_.empty()) {
            cond_.wait(lock, [this] {
                return stopRequested_ || !threadInfos_.empty();
            });
            continue;
        }
        cond_.wait_for(lock, config_.pollInterval, [this] {
            return stopRequested_;
        });
        if (stopRequested_) {
            break;
        }
        lock.unlock();
        reportRunInfo();
        lock.lock();
    }
}

bool ThreadManager::add(const std::shared_ptr<Thread>& thread) noexcept {
    if (!thread || thread->hasExited()) {
        return false;
    }
    bool inserted = false;
    {
        std::lock_guard lock(mutex_);
        inserted = threadInfos_.insert_or_assign(thread->serial(), thread).second;
    }
    cond_.notify_one();
    return inserted;
}

bool ThreadManager::remove(const std::shared_ptr<Thread>& thread) noexcept {
    if (!thread) {
        return false;
    }
    bool erased = false;
    {
        std::lock_guard lock(mutex_);
        erased = threadInfos_.erase(thread->serial()) > 0;
    }
    return erased;
}

size_t ThreadManager::size() const noexcept {
    std::lock_guard lock(mutex_);
    return static_cast<size_t>(std::ranges::count_if(threadInfos_, [](const auto& entry) {
        return !entry.second.expired();
    }));
}

void ThreadManager::setBlockCallback(BlockCallback callback) noexcept {
    std::lock_guard lock(mutex_);
    blockCallback_ = std::move(callback);
}

void ThreadManager::reportRunInfo() noexcept {
    const auto now = Time::nowTimeStamp();
    // Strong refs are released after unlocking: ~Thread joins a worker that unregisters under it.
    std::vector<std::shared_ptr<Thread>> threads;
    BlockCallback callback;
    {
        std::lock_guard lock(mutex_);
        for (auto it = threadInfos_.begin(); it != threadInfos_.end();) {
            if (it->second.expired()) {
                it = threadInfos_.erase(it);
                continue;
            }
            if (auto thread = it->second.lock()) {
                threads.push_back(std::move(thread));
            }
            ++it;
        }
        callback = blockCallback_;
    }

    for (const auto& thread : threads) {
        if (!thread->isRunning() || !thread->isActive()) {
            continue;
        }
        const auto interval = now - thread->getLastRunTimeStamp();
        if (interval < config_.blockThreshold) {
            continue;
        }
        const auto name = thread->getName();
        LogE("ThreadManager report [{}] is blocking for {}ms.", name,
             interval.toMilliSeconds().count());
        if (callback) {
            callback(name, interval);
        }
    }
}

}  // namespace slark
