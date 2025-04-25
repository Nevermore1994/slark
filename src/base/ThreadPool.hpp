//
// Created by Nevermore on 2025/4/23.
//

#pragma once
#include <condition_variable>
#include <vector>
#include <thread>
#include <type_traits>
#include <queue>
#include <functional>
#include <mutex>
#include <algorithm>
#include <memory>
#include <future>
#include <expected>
#include "Log.hpp"
#include "NonCopyable.h"

namespace slark {

struct ThreadPoolConfig {
    uint32_t threadCount = 4;
    uint32_t maxQueueSize = 10000;
    std::chrono::milliseconds timeout{100};
};

class ThreadPool: public NonCopyable {
public:
    explicit ThreadPool(const ThreadPoolConfig& config = ThreadPoolConfig{});
    ~ThreadPool() override {
        {
            std::unique_lock<std::mutex> lock(mutex_);
            shutdown_ = true;
        }

        notEmpty_.notify_all();
        for (auto& worker : workers_) {
            if (worker.joinable()) {
                worker.join();
            }
        }
    }

    template<class F, typename... Args>
    auto submit(F&& f, Args&&... args) -> std::expected<std::future<std::invoke_result_t<F, Args...>>, bool>;

    void waitForAll() noexcept {
        std::unique_lock<std::mutex> lock(mutex_);
        notFull_.wait(lock, [this] {
            return tasks_.empty() && activeThreads_ == 0;
        });
    }

    size_t getTaskCount() const noexcept {
        std::lock_guard<std::mutex> lock(mutex_);
        return tasks_.size();
    }

    size_t getThreadCount() const noexcept {
        return workers_.size();
    }

    bool isShutdown() const noexcept {
        return shutdown_;
    }

private:
    void initThreads();
    void workThread();

    std::vector<std::thread> workers_;
    std::queue<std::function<void()>> tasks_;
    
    mutable std::mutex mutex_;
    std::condition_variable notEmpty_;
    std::condition_variable notFull_;
    
    std::atomic<bool> shutdown_{false};
    std::atomic<size_t> activeThreads_{0};
    
    ThreadPoolConfig config_;
};

inline ThreadPool::ThreadPool(const ThreadPoolConfig& config) : config_(config) {
    initThreads();
}

inline void ThreadPool::initThreads() {
    for (uint32_t i = 0; i < config_.threadCount; ++i) {
        workers_.emplace_back([this] { workThread(); });
    }
}

inline void ThreadPool::workThread() {
    while (true) {
        std::function<void()> task;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            notEmpty_.wait(lock, [this] {
                return shutdown_ || !tasks_.empty();
            });

            if (shutdown_ && tasks_.empty()) {
                return;
            }

            task = std::move(tasks_.front());
            tasks_.pop();
            notFull_.notify_one();
        }

        ++activeThreads_;
        try {
            task();
        } catch (...) {
            LogE("error in executing task");
        }
        --activeThreads_;
    }
}

template<class F, typename... Args>
auto ThreadPool::submit(F&& f, Args&&... args) 
    -> std::expected<std::future<std::invoke_result_t<F, Args...>>, bool>
{
    using ReturnType = std::invoke_result_t<F, Args...>;
    
    auto task = std::make_shared<std::packaged_task<ReturnType()>>(
        std::bind(std::forward<F>(f), std::forward<Args>(args)...)
    );

    std::future<ReturnType> future = task->get_future();
    {
        std::unique_lock<std::mutex> lock(mutex_);

        if (!notFull_.wait_for(lock, config_.timeout, [this] {
            return tasks_.size() < config_.maxQueueSize || shutdown_;
        })) {
            LogE("The task queue is full");
            return std::unexpected(false);
        }

        if (shutdown_) {
            LogE("The thread pool is exiting");
            return std::unexpected(false);
        }
        tasks_.emplace([task]() { (*task)(); });
    }
    
    notEmpty_.notify_one();
    return future;
}

}