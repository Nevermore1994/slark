//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//
#pragma once
#include "Error.h"
#include "Log.hpp"
#include "MoveOnlyFunction.hpp"
#include "NonCopyable.h"

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <future>
#include <mutex>
#include <thread>
#include <vector>

namespace slark {

enum class JobPriority : uint8_t {
    High = 0,
    Normal = 1,
    Low = 2,
};

struct ThreadPoolConfig {
    uint32_t threadCount = 4;
};

class ThreadPool : public NonCopyMovable {
public:
    explicit ThreadPool(const ThreadPoolConfig& config = ThreadPoolConfig{});

    ~ThreadPool() {
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

    template <class F, typename... Args>
    auto submit(F&& f, Args&&... args) -> Expected<std::future<std::invoke_result_t<F, Args...>>> {
        return submitWithPriority(JobPriority::Normal,
                                  std::forward<F>(f),
                                  std::forward<Args>(args)...);
    }

    template <class F, typename... Args>
    auto submit(JobPriority priority, F&& f, Args&&... args)
        -> Expected<std::future<std::invoke_result_t<F, Args...>>> {
        return submitWithPriority(priority, std::forward<F>(f), std::forward<Args>(args)...);
    }

    template <class F, typename... Args>
        requires std::is_invocable_v<F, Args...>
    Expected<void> postJob(F&& f, Args&&... args) {
        return postJobWithPriority(JobPriority::Normal,
                                   std::forward<F>(f),
                                   std::forward<Args>(args)...);
    }

    template <class F, typename... Args>
        requires std::is_invocable_v<F, Args...>
    Expected<void> postJob(JobPriority priority, F&& f, Args&&... args) {
        return postJobWithPriority(priority, std::forward<F>(f), std::forward<Args>(args)...);
    }

    size_t getJobCount() const noexcept {
        std::lock_guard<std::mutex> lock(mutex_);
        return jobs_.size();
    }

    void waitForAll() noexcept {
        std::unique_lock<std::mutex> lock(mutex_);
        allJobsDone_.wait(lock, [this] {
            return jobs_.empty() && activeCount_ == 0;
        });
    }

    // 移除所有待执行任务，并异步触发取消回调
    // 取消回调在独立线程中执行，避免阻塞调用线程
    void clear() noexcept {
        std::vector<Job> jobsToCancel;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            jobsToCancel = std::move(jobs_);
            jobs_.clear();
        }
        // 异步执行取消回调，避免阻塞调用线程
        if (!jobsToCancel.empty()) {
            postJob([jobs = std::move(jobsToCancel)]() {
                for (auto& job : jobs) {
                    if (job.cancel) {
                        job.cancel();
                    }
                }
            });
        }
    }

    size_t getThreadCount() const noexcept {
        return workers_.size();
    }

    bool isShutdown() const noexcept {
        return shutdown_;
    }

private:
    struct Job {
        JobPriority priority;
        uint64_t sequence;
        MoveOnlyFunction<void()> func;
        MoveOnlyFunction<void()> cancel; // 取消回调，用于让 future 返回

        bool operator>(const Job& other) const noexcept {
            if (priority != other.priority) {
                return static_cast<uint8_t>(priority) > static_cast<uint8_t>(other.priority);
            }
            return sequence > other.sequence;
        }
    };

    template <class F, typename... Args>
    auto submitWithPriority(JobPriority priority, F&& f, Args&&... args)
        -> Expected<std::future<std::invoke_result_t<F, Args...>>>;

    template <class F, typename... Args>
        requires std::is_invocable_v<F, Args...>
    Expected<void> postJobWithPriority(JobPriority priority, F&& f, Args&&... args);

    void workThread();

    std::vector<std::thread> workers_;
    std::vector<Job> jobs_;
    mutable std::mutex mutex_;
    std::condition_variable notEmpty_;
    std::condition_variable allJobsDone_;
    std::atomic<bool> shutdown_{false};
    std::atomic<size_t> activeCount_{0};
    std::atomic<uint64_t> sequenceCounter_{0};
};

inline ThreadPool::ThreadPool(const ThreadPoolConfig& config) {
    for (uint32_t i = 0; i < config.threadCount; ++i) {
        workers_.emplace_back([this] {
            workThread();
        });
    }
}

inline void ThreadPool::workThread() {
    while (true) {
        MoveOnlyFunction<void()> job;

        {
            std::unique_lock<std::mutex> lock(mutex_);
            notEmpty_.wait(lock, [this] {
                return !jobs_.empty() || shutdown_.load();
            });

            if (shutdown_.load() && jobs_.empty()) {
                return;
            }
            std::pop_heap(jobs_.begin(), jobs_.end(), std::greater<>{});
            job = std::move(jobs_.back().func);
            jobs_.pop_back();
        }

        ++activeCount_;
        try {
            job();
        } catch (...) {
            LogE("error in executing job");
        }

        {
            std::lock_guard<std::mutex> lock(mutex_);
            --activeCount_;
            if (jobs_.empty() && activeCount_ == 0) {
                allJobsDone_.notify_all();
            }
        }
    }
}

template <class F, typename... Args>
auto ThreadPool::submitWithPriority(JobPriority priority, F&& f, Args&&... args)
    -> Expected<std::future<std::invoke_result_t<F, Args...>>> {
    using ReturnType = std::invoke_result_t<F, Args...>;

    // 用 promise 持有 shared state，cancel 时可直接设置异常而不影响 future 绑定
    auto promise = std::make_shared<std::promise<ReturnType>>();
    std::future<ReturnType> future = promise->get_future();

    {
        std::unique_lock<std::mutex> lock(mutex_);
        if (shutdown_) {
            LogE("The thread pool is exiting");
            return std::unexpected(Error::InvalidOperation);
        }

        auto fn = [f = std::forward<F>(f), ... capturedArgs = std::forward<Args>(args)]() mutable {
            return f(std::move(capturedArgs)...);
        };

        jobs_.push_back(Job{
            .priority = priority,
            .sequence = sequenceCounter_.fetch_add(1, std::memory_order_relaxed),
            .func =
                [task = std::move(fn), promise]() mutable {
                    try {
                        if constexpr (std::is_void_v<ReturnType>) {
                            task();
                            promise->set_value();
                        } else {
                            promise->set_value(task());
                        }
                    } catch (...) {
                        promise->set_exception(std::current_exception());
                    }
                },
            .cancel =
                [promise]() {
                    promise->set_exception(
                        std::make_exception_ptr(std::runtime_error("Job cancelled")));
                },
        });
        std::push_heap(jobs_.begin(), jobs_.end(), std::greater<>{});
    }

    notEmpty_.notify_one();
    return future;
}

template <class F, typename... Args>
    requires std::is_invocable_v<F, Args...>
Expected<void> ThreadPool::postJobWithPriority(JobPriority priority, F&& f, Args&&... args) {
    {
        std::unique_lock<std::mutex> lock(mutex_);
        if (shutdown_) {
            return std::unexpected(Error::InvalidOperation);
        }

        jobs_.push_back(Job{
            .priority = priority,
            .sequence = sequenceCounter_.fetch_add(1, std::memory_order_relaxed),
            .func =
                [fn = std::forward<F>(f), ... capturedArgs = std::forward<Args>(args)]() mutable {
                    fn(std::move(capturedArgs)...);
                },
            .cancel = {},
        });
        std::push_heap(jobs_.begin(), jobs_.end(), std::greater<>{});
    }

    notEmpty_.notify_one();
    return {};
}

} // namespace slark
