//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#pragma once

#include "Thread.h"

#include <functional>
#include <mutex>
#include <thread>
#include <unordered_map>

namespace slark {

static const Time::TimeDelta kMaxThreadBlockTimeInterval =
    Time::TimeDelta::fromSeconds(30.0); // 30s

class ThreadManager final : public NonCopyMovable {
public:
    using BlockCallback =
        std::function<void(std::string_view threadName, Time::TimeDelta blockDuration)>;

    static inline ThreadManager& shareInstance() {
        static ThreadManager instance;
        return instance;
    }

public:
    ThreadManager();

    ~ThreadManager();

    void add(const std::shared_ptr<Thread>& thread) noexcept;

    void remove(const std::shared_ptr<Thread>& thread) noexcept;

    void remove(std::thread::id id) noexcept;

    std::shared_ptr<Thread> thisThread() noexcept;

    // 设置线程阻塞回调，业务层可据此上报/abort
    void setBlockCallback(BlockCallback callback) noexcept;

private:
    void monitorLoop();

    void reportRunInfo() noexcept;

private:
    std::unordered_map<std::thread::id, std::weak_ptr<Thread>> threadInfos_;
    std::mutex mutex_;
    std::condition_variable cond_;
    std::thread monitorThread_;
    std::atomic<bool> stopRequested_{false};
    BlockCallback blockCallback_;
};

} // namespace slark
