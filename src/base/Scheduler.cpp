//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//
#include "Scheduler.h"

#include <utility>

#include "Log.hpp"
#include "TaskThread.h"

namespace slark {

ThreadScheduler::ThreadScheduler(std::string name) : thread_(TaskThread::create(std::move(name))) {
    if (!thread_) {
        LogE("ThreadScheduler: create worker thread failed");
    }
}

ThreadScheduler::~ThreadScheduler() {
    stop();
}

void ThreadScheduler::stop() noexcept {
    if (!thread_) {
        return;
    }
    thread_->stop();
    thread_->join();
    std::lock_guard lock(mutex_);
    slots_.clear();
}

void ThreadScheduler::execute(Task task) noexcept {
    if (thread_ && task) {
        (void)thread_->postTask(ThreadTask(std::move(task)));
    }
}

void ThreadScheduler::postAt(Slot slot, Time::TimePoint at, Task task) {
    if (!thread_ || !task) {
        return;
    }
    std::lock_guard lock(mutex_);
    if (auto it = slots_.find(slot); it != slots_.end()) {
        thread_->cancel(it->second);
    }
    slots_[slot] = thread_->postTask(at, ThreadTask(std::move(task)));
}

void ThreadScheduler::cancel(Slot slot) {
    if (!thread_) {
        return;
    }
    std::lock_guard lock(mutex_);
    if (auto it = slots_.find(slot); it != slots_.end()) {
        thread_->cancel(it->second);
        slots_.erase(it);
    }
}

Time::TimePoint ThreadScheduler::now() const noexcept {
    return Time::nowTimeStamp();
}

bool ThreadScheduler::onSchedulerThread() const noexcept {
    return thread_ && thread_->isCurrentThread();
}

}  // namespace slark
