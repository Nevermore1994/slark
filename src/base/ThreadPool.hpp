//
// Created by Nevermore on 2025/4/23.
//

#pragma once
#include "Error.h"
#include "Log.hpp"
#include "MoveOnlyFunction.hpp"
#include "NonCopyable.h"

#include <algorithm>
#include <condition_variable>
#include <future>
#include <mutex>
#include <thread>
#include <vector>

namespace slark {

enum class TaskPriority : uint8_t {
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

    template <class F, typename... Args>
    auto submit(F&& f, Args&&... args) -> Expected<std::future<std::invoke_result_t<F, Args...>>> {
        return submitWithPriority(TaskPriority::Normal,
                                  std::forward<F>(f),
                                  std::forward<Args>(args)...);
    }

    template <class F, typename... Args>
    auto submit(TaskPriority priority, F&& f, Args&&... args)
        -> Expected<std::future<std::invoke_result_t<F, Args...>>> {
        return submitWithPriority(priority, std::forward<F>(f), std::forward<Args>(args)...);
    }

    /// fire-and-forget 任务投递
    template <class F, typename... Args>
        requires std::is_invocable_v<F, Args...>
    Expected<void> postTask(F&& f, Args&&... args) {
        return postTaskWithPriority(TaskPriority::Normal,
                                    std::forward<F>(f),
                                    std::forward<Args>(args)...);
    }

    template <class F, typename... Args>
        requires std::is_invocable_v<F, Args...>
    Expected<void> postTask(TaskPriority priority, F&& f, Args&&... args) {
        return postTaskWithPriority(priority, std::forward<F>(f), std::forward<Args>(args)...);
    }

    size_t getTaskCount() const noexcept {
        std::lock_guard<std::mutex> lock(mutex_);
        return heap_.size();
    }

    void waitForAll() noexcept {
        std::unique_lock<std::mutex> lock(mutex_);
        allTasksDone_.wait(lock, [this] {
            return heap_.empty() && activeThreads_ == 0;
        });
    }

    void clear() noexcept {
        std::lock_guard<std::mutex> lock(mutex_);
        heap_.clear();
    }

    size_t getThreadCount() const noexcept {
        return workers_.size();
    }

    bool isShutdown() const noexcept {
        return shutdown_;
    }

private:
    struct PrioritizedTask {
        TaskPriority priority;
        uint64_t sequence;
        MoveOnlyFunction<void()> func;

        // 小顶堆：优先级低值优先，同优先级 sequence 小的优先
        bool operator>(const PrioritizedTask& other) const noexcept {
            if (priority != other.priority) {
                return static_cast<uint8_t>(priority) > static_cast<uint8_t>(other.priority);
            }
            return sequence > other.sequence;
        }
    };

    static constexpr auto kHeapCmp = std::greater<>{};

    template <class F, typename... Args>
    auto submitWithPriority(TaskPriority priority, F&& f, Args&&... args)
        -> Expected<std::future<std::invoke_result_t<F, Args...>>>;

    template <class F, typename... Args>
        requires std::is_invocable_v<F, Args...>
    Expected<void> postTaskWithPriority(TaskPriority priority, F&& f, Args&&... args);

    void enqueueTask(PrioritizedTask task) noexcept;
    PrioritizedTask dequeueTask() noexcept;

    void initThreads();
    void workThread(std::stop_token stopToken);

    std::vector<std::jthread> workers_;
    std::vector<PrioritizedTask> heap_;

    mutable std::mutex mutex_;
    std::condition_variable notEmpty_;
    std::condition_variable allTasksDone_;

    std::atomic<bool> shutdown_{false};
    std::atomic<size_t> activeThreads_{0};
    std::atomic<uint64_t> sequenceCounter_{0};

    ThreadPoolConfig config_;
};

inline ThreadPool::ThreadPool(const ThreadPoolConfig& config) : config_(config) {
    initThreads();
}

inline void ThreadPool::initThreads() {
    for (uint32_t i = 0; i < config_.threadCount; ++i) {
        workers_.emplace_back([this](std::stop_token token) {
            workThread(token);
        });
    }
}

inline void ThreadPool::enqueueTask(PrioritizedTask task) noexcept {
    heap_.push_back(std::move(task));
    std::push_heap(heap_.begin(), heap_.end(), kHeapCmp);
}

inline ThreadPool::PrioritizedTask ThreadPool::dequeueTask() noexcept {
    std::pop_heap(heap_.begin(), heap_.end(), kHeapCmp);
    auto task = std::move(heap_.back());
    heap_.pop_back();
    return task;
}

inline void ThreadPool::workThread(std::stop_token stopToken) {
    while (true) {
        MoveOnlyFunction<void()> task;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            notEmpty_.wait(lock, [this, &stopToken] {
                return (shutdown_ && heap_.empty()) || !heap_.empty() || stopToken.stop_requested();
            });

            if ((shutdown_ || stopToken.stop_requested()) && heap_.empty()) {
                return;
            }

            auto pt = dequeueTask();
            task = std::move(pt.func);
        }

        ++activeThreads_;
        try {
            task();
        } catch (...) {
            LogE("error in executing task");
        }
        --activeThreads_;
        allTasksDone_.notify_all();
    }
}

template <class F, typename... Args>
auto ThreadPool::submitWithPriority(TaskPriority priority, F&& f, Args&&... args)
    -> Expected<std::future<std::invoke_result_t<F, Args...>>> {
    using ReturnType = std::invoke_result_t<F, Args...>;

    auto task = std::make_shared<std::packaged_task<ReturnType()>>(
        [fn = std::forward<F>(f), ... capturedArgs = std::forward<Args>(args)]() mutable {
            return fn(std::move(capturedArgs)...);
        });

    std::future<ReturnType> future = task->get_future();
    {
        std::unique_lock<std::mutex> lock(mutex_);
        if (shutdown_) {
            LogE("The thread pool is exiting");
            return std::unexpected(DataError::InvalidOperation);
        }
        enqueueTask(PrioritizedTask{
            .priority = priority,
            .sequence = sequenceCounter_.fetch_add(1, std::memory_order_relaxed),
            .func =
                [task]() {
                    (*task)();
                },
        });
    }

    notEmpty_.notify_one();
    return future;
}

template <class F, typename... Args>
    requires std::is_invocable_v<F, Args...>
Expected<void> ThreadPool::postTaskWithPriority(TaskPriority priority, F&& f, Args&&... args) {
    {
        std::unique_lock<std::mutex> lock(mutex_);
        if (shutdown_) {
            return std::unexpected(DataError::InvalidOperation);
        }
        enqueueTask(PrioritizedTask{
            .priority = priority,
            .sequence = sequenceCounter_.fetch_add(1, std::memory_order_relaxed),
            .func =
                [fn = std::forward<F>(f), ... capturedArgs = std::forward<Args>(args)]() mutable {
                    fn(std::move(capturedArgs)...);
                },
        });
    }
    notEmpty_.notify_one();
    return {};
}

} // namespace slark
