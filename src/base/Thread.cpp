//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#include "Thread.h"

#include <pthread.h>

#include <utility>
#include <vector>

namespace slark {
namespace {
thread_local Thread* tlsCurrentWorker = nullptr;
std::atomic<uint64_t> nextSerial{1};

struct CompletionState {
    std::mutex mutex;
    std::condition_variable cond;
    bool completed = false;
    bool abandoned = false;
};

struct CompletionTask {
    CompletionTask(std::shared_ptr<CompletionState> completionState, ThreadTask work)
        : state(std::move(completionState)), task(std::move(work)) {}

    CompletionTask(CompletionTask&& other) noexcept
        : state(std::move(other.state)), task(std::move(other.task)), completed(other.completed) {
        other.completed = true;
    }

    CompletionTask(const CompletionTask&) = delete;
    CompletionTask& operator=(const CompletionTask&) = delete;

    std::shared_ptr<CompletionState> state;
    ThreadTask task;
    bool completed = false;

    void operator()() {
        if (task) {
            task();
        }
        {
            std::lock_guard lock(state->mutex);
            state->completed = true;
            completed = true;
        }
        state->cond.notify_all();
    }

    ~CompletionTask() {
        if (!state || completed) {
            return;
        }
        {
            std::lock_guard lock(state->mutex);
            state->abandoned = true;
        }
        state->cond.notify_all();
    }
};
}  // namespace

Thread::Thread(std::string name, WakeHook wakeHook)
    : name_(std::move(name)), wakeHook_(std::move(wakeHook)), serial_(nextSerial.fetch_add(1)) {}

Thread::~Thread() noexcept {
    stop();
    std::lock_guard lock(joinMutex_);
    if (!worker_.joinable()) {
        return;
    }
    if (isCurrentThread()) {
        worker_.detach();
    } else {
        worker_.join();
    }
}

bool Thread::isRunning() const noexcept {
    return !isStopRequested() && !hasExited();
}

bool Thread::isStopRequested() const noexcept {
    return stopRequested_.load(std::memory_order_acquire);
}

bool Thread::hasExited() const noexcept {
    return exited_.load(std::memory_order_acquire);
}

bool Thread::isCurrentThread() const noexcept {
    return getId() == std::this_thread::get_id();
}

void Thread::stop() noexcept {
    {
        std::lock_guard lock(mutex_);
        if (stopRequested_.exchange(true, std::memory_order_acq_rel)) {
            return;
        }
    }
    cond_.notify_all();
    invokeWakeHook();
}

void Thread::join() noexcept {
    if (isCurrentThread()) {
        return;
    }
    std::lock_guard lock(joinMutex_);
    if (worker_.joinable()) {
        worker_.join();
    }
}

Expected<void> Thread::postTask(ThreadTask task) noexcept {
    {
        std::lock_guard lock(mutex_);
        if (stopRequested_.load(std::memory_order_acquire) ||
            exited_.load(std::memory_order_acquire)) {
            return std::unexpected(Error::InvalidOperation);
        }
        taskQueue_.push(std::move(task));
    }
    cond_.notify_one();
    invokeWakeHook();
    return {};
}

Expected<void> Thread::postAndWait(ThreadTask task) noexcept {
    if (isCurrentThread()) {
        if (task) {
            task();
        }
        return {};
    }

    auto state = std::make_shared<CompletionState>();
    auto result = postTask(ThreadTask(CompletionTask{state, std::move(task)}));
    if (!result) {
        return std::unexpected(Error::Cancelled);
    }

    std::unique_lock lock(state->mutex);
    state->cond.wait(lock, [&] {
        return state->completed || state->abandoned;
    });
    return state->completed ? Expected<void>{} : Expected<void>{std::unexpected(Error::Cancelled)};
}

std::string Thread::getName() const noexcept {
    std::lock_guard lock(mutex_);
    return name_;
}

void Thread::setThreadName(std::string_view name) noexcept {
    {
        std::lock_guard lock(mutex_);
        name_ = std::string(name);
    }
    nameDirty_.store(true, std::memory_order_release);
    notifyWorker();
}

Thread::ScopedInactive::ScopedInactive() noexcept : thread_(Thread::current()) {
    if (thread_) {
        thread_->setActive(false);
    }
}

Thread::ScopedInactive::~ScopedInactive() noexcept {
    if (thread_) {
        thread_->setActive(true);
    }
}

void Thread::setCurrentActive(bool active) noexcept {
    if (auto* thread = current()) {
        thread->setActive(active);
    }
}

void Thread::startWorker(ThreadTask body) {
    worker_ = std::thread([weak = weak_from_this(), body = std::move(body)]() mutable {
        if (auto self = weak.lock()) {
            self->runWorker(std::move(body));
        }
    });
}

void Thread::drainPendingWork() noexcept {
    if (nameDirty_.exchange(false, std::memory_order_acq_rel)) {
        setup();
    }
    auto tasks = taskQueue_.tryPopAll();
    for (auto& task : tasks) {
        if (task) {
            task();
        }
    }
}

bool Thread::hasPendingWork() const noexcept {
    return !taskQueue_.empty();
}

void Thread::notifyWorker() noexcept {
    notify();
    invokeWakeHook();
}

void Thread::notify() noexcept {
    {
        std::lock_guard lock(mutex_);
    }
    cond_.notify_all();
}

void Thread::setActive(bool active) noexcept {
    if (active) {
        lastRunTimeStamp_.store(Time::nowTimeStamp().point(), std::memory_order_release);
    }
    isActive_.store(active, std::memory_order_release);
}

Thread* Thread::current() noexcept {
    return tlsCurrentWorker;
}

void Thread::runWorker(ThreadTask body) noexcept {
    tlsCurrentWorker = this;
    id_.store(std::this_thread::get_id(), std::memory_order_release);
    setup();
    setActive(true);

    if (body) {
        body();
    }

    std::vector<ThreadTask> abandoned;
    {
        std::lock_guard lock(mutex_);
        stopRequested_.store(true, std::memory_order_release);
        abandoned = taskQueue_.tryPopAll();
    }
    abandoned.clear();
    setActive(false);
    tlsCurrentWorker = nullptr;
    {
        std::lock_guard lock(mutex_);
        exited_.store(true, std::memory_order_release);
    }
    cond_.notify_all();
}

void Thread::setup() noexcept {
    std::string name;
    {
        std::lock_guard lock(mutex_);
        name = name_;
    }
#ifndef __APPLE__
    auto truncated = name.substr(0, 15);
    pthread_setname_np(pthread_self(), truncated.c_str());
#else
    pthread_setname_np(name.c_str());
#endif
}

void Thread::invokeWakeHook() noexcept {
    if (wakeHook_) {
        wakeHook_();
    }
}

namespace _channel_detail {
void setCurrentThreadActive(bool active) noexcept {
    Thread::setCurrentActive(active);
}
}  // namespace _channel_detail

}  // namespace slark
