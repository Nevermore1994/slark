//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#include "LoopThread.h"

namespace slark {

LoopThread::LoopThread(std::string name, Loop loop, WakeHook wakeHook)
    : Thread(std::move(name), std::move(wakeHook)), loop_(std::move(loop)) {}

std::shared_ptr<LoopThread> LoopThread::create(std::string name, Loop loop, WakeHook wakeHook) {
    auto thread = std::shared_ptr<LoopThread>(
        new LoopThread(std::move(name), std::move(loop), std::move(wakeHook)));
    try {
        thread->startWorker(ThreadTask([ptr = thread.get()] {
            ptr->runLoop();
        }));
    } catch (...) {
        return nullptr;
    }
    return thread;
}

void LoopThread::notify() noexcept {
    notifyWorker();
}

void LoopThread::runLoop() noexcept {
    if (loop_) {
        loop_(*this);
    }
}

}  // namespace slark
