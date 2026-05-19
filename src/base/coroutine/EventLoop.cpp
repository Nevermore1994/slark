//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//
#include "EventLoop.h"

#include "CoroutineScheduler.hpp"
#include "Log.hpp"

#include <cassert>
#include <fcntl.h>
#ifdef __linux__
#include <sys/eventfd.h>
#endif
#include <unistd.h>

namespace slark {

EventLoop::EventLoop() {
#ifdef __linux__
    epollFd_ = epoll_create1(EPOLL_CLOEXEC);
    if (epollFd_ < 0) {
        LogE("Failed to create epoll fd");
    }
    // 创建 eventfd 用于唤醒
    wakeupFd_ = ::eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
    if (wakeupFd_ >= 0) {
        epoll_event event{};
        event.events = EPOLLIN;
        event.data.fd = wakeupFd_;
        epoll_ctl(epollFd_, EPOLL_CTL_ADD, wakeupFd_, &event);
    }
#elif defined(__APPLE__)
    kqueueFd_ = kqueue();
    if (kqueueFd_ < 0) {
        LogE("Failed to create kqueue fd");
    }
    // macOS 使用 EVFILT_USER 唤醒，无需 pipe
#endif
}

EventLoop::~EventLoop() {
    stop();
#ifdef __linux__
    if (wakeupFd_ >= 0) {
        ::close(wakeupFd_);
    }
    if (epollFd_ >= 0) {
        ::close(epollFd_);
    }
#elif defined(__APPLE__)
    if (kqueueFd_ >= 0) {
        ::close(kqueueFd_);
    }
#endif
}

void EventLoop::run() noexcept {
    running_.store(true, std::memory_order_release);
    while (running_.load(std::memory_order_acquire)) {
        poll();
    }
}

void EventLoop::stop() noexcept {
    running_.store(false, std::memory_order_release);
    // 唤醒 poll 立即退出
#ifdef __linux__
    if (wakeupFd_ >= 0) {
        uint64_t value = 1;
        ::write(wakeupFd_, &value, sizeof(value));
    }
#elif defined(__APPLE__)
    if (kqueueFd_ >= 0) {
        struct kevent change;
        EV_SET(&change, 0, EVFILT_USER, EV_ADD | EV_ENABLE, NOTE_TRIGGER, 0, nullptr);
        kevent(kqueueFd_, &change, 1, nullptr, 0, nullptr);
    }
#endif
}

void EventLoop::registerFd(int32_t fd) noexcept {
    int32_t flags = fcntl(fd, F_GETFL, 0);
    if (flags >= 0) {
        fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    }

#ifdef __linux__
    epoll_event event{};
    event.events = EPOLLET;
    event.data.fd = fd;
    epoll_ctl(epollFd_, EPOLL_CTL_ADD, fd, &event);
#elif defined(__APPLE__)
    // kqueue 在设置事件时自动注册
#endif
}

void EventLoop::unregisterFd(int32_t fd) noexcept {
    unregisterAllContinuations(fd);

#ifdef __linux__
    {
        std::lock_guard<std::mutex> lock(fdEventsMutex_);
        fdEvents_.erase(fd);
    }
    epoll_ctl(epollFd_, EPOLL_CTL_DEL, fd, nullptr);
#elif defined(__APPLE__)
    struct kevent changes[2];
    EV_SET(&changes[0], fd, EVFILT_READ, EV_DELETE, 0, 0, nullptr);
    EV_SET(&changes[1], fd, EVFILT_WRITE, EV_DELETE, 0, 0, nullptr);
    kevent(kqueueFd_, changes, 2, nullptr, 0, nullptr);
#endif
}

void EventLoop::setReadable(int32_t fd, bool enabled) noexcept {
#ifdef __linux__
    {
        std::lock_guard<std::mutex> lock(fdEventsMutex_);
        if (enabled) {
            fdEvents_[fd] |= EPOLLIN;
        } else {
            fdEvents_[fd] &= ~EPOLLIN;
        }
        epoll_event event{};
        event.data.fd = fd;
        event.events = EPOLLET | fdEvents_[fd];
        epoll_ctl(epollFd_, EPOLL_CTL_MOD, fd, &event);
    }
#elif defined(__APPLE__)
    struct kevent change;
    EV_SET(&change, fd, EVFILT_READ, enabled ? EV_ADD | EV_ENABLE : EV_DELETE, 0, 0, nullptr);
    kevent(kqueueFd_, &change, 1, nullptr, 0, nullptr);
#endif
}

void EventLoop::setWritable(int32_t fd, bool enabled) noexcept {
#ifdef __linux__
    {
        std::lock_guard<std::mutex> lock(fdEventsMutex_);
        if (enabled) {
            fdEvents_[fd] |= EPOLLOUT;
        } else {
            fdEvents_[fd] &= ~EPOLLOUT;
        }
        epoll_event event{};
        event.data.fd = fd;
        event.events = EPOLLET | fdEvents_[fd];
        epoll_ctl(epollFd_, EPOLL_CTL_MOD, fd, &event);
    }
#elif defined(__APPLE__)
    struct kevent change;
    EV_SET(&change, fd, EVFILT_WRITE, enabled ? EV_ADD | EV_ENABLE : EV_DELETE, 0, 0, nullptr);
    kevent(kqueueFd_, &change, 1, nullptr, 0, nullptr);
#endif
}

void EventLoop::registerContinuation(int32_t fd,
                                     FdInterest interest,
                                     std::coroutine_handle<> handle) noexcept {
    {
        std::lock_guard<std::mutex> lock(continuationMutex_);
        auto& waiters = continuations_[fd];
        if (interest == FdInterest::Read) {
            assert((!waiters.readHandle || waiters.readHandle.done()) &&
                   "Overwriting pending read waiter on fd");
            if (waiters.readHandle && !waiters.readHandle.done()) {
                LogW("EventLoop: overwriting pending read waiter on fd={}", fd);
            }
            waiters.readHandle = handle;
        } else {
            assert((!waiters.writeHandle || waiters.writeHandle.done()) &&
                   "Overwriting pending write waiter on fd");
            if (waiters.writeHandle && !waiters.writeHandle.done()) {
                LogW("EventLoop: overwriting pending write waiter on fd={}", fd);
            }
            waiters.writeHandle = handle;
        }
    }

    // 更新事件关注状态
    {
        std::lock_guard<std::mutex> lock(interestMutex_);
        auto& state = interestStates_[fd];
        if (interest == FdInterest::Read) {
            state.hasReadWaiter = true;
        } else {
            state.hasWriteWaiter = true;
        }
    }
}

void EventLoop::unregisterContinuation(int32_t fd, FdInterest interest) noexcept {
    bool needDisableRead = false;
    bool needDisableWrite = false;

    {
        std::lock_guard<std::mutex> lock(continuationMutex_);
        auto it = continuations_.find(fd);
        if (it == continuations_.end()) {
            return;
        }
        if (interest == FdInterest::Read) {
            it->second.readHandle = nullptr;
        } else {
            it->second.writeHandle = nullptr;
        }
        if (it->second.empty()) {
            continuations_.erase(it);
        }
    }

    // 更新事件关注状态并检查是否需要收敛
    {
        std::lock_guard<std::mutex> lock(interestMutex_);
        auto it = interestStates_.find(fd);
        if (it == interestStates_.end()) {
            return;
        }

        auto& state = it->second;
        if (interest == FdInterest::Read) {
            state.hasReadWaiter = false;
            needDisableRead = true;
        } else {
            state.hasWriteWaiter = false;
            needDisableWrite = true;
        }

        // 如果没有等待者了，清理状态
        if (!state.hasReadWaiter && !state.hasWriteWaiter) {
            interestStates_.erase(it);
        }
    }

    // 在锁外更新系统事件，避免死锁
    if (needDisableRead) {
        setReadable(fd, false);
    }
    if (needDisableWrite) {
        setWritable(fd, false);
    }
}

void EventLoop::unregisterAllContinuations(int32_t fd) noexcept {
    {
        std::lock_guard<std::mutex> lock(continuationMutex_);
        continuations_.erase(fd);
    }
    {
        std::lock_guard<std::mutex> lock(interestMutex_);
        interestStates_.erase(fd);
    }
}

EventLoop::ExtractedHandles EventLoop::extractHandles(int32_t fd,
                                                      bool readable,
                                                      bool writable,
                                                      bool error) noexcept {
    ExtractedHandles result;

    std::lock_guard<std::mutex> lock(continuationMutex_);
    auto it = continuations_.find(fd);
    if (it == continuations_.end()) {
        return result;
    }

    auto& waiters = it->second;

    // 按事件类型取出对应等待者，一次性触发后移除
    if ((readable || error) && waiters.readHandle) {
        result.readHandle = waiters.readHandle;
        waiters.readHandle = nullptr;
        result.needDisableRead = true;
    }
    if ((writable || error) && waiters.writeHandle) {
        result.writeHandle = waiters.writeHandle;
        waiters.writeHandle = nullptr;
        result.needDisableWrite = true;
    }

    if (waiters.empty()) {
        continuations_.erase(it);
    }

    return result;
}

void EventLoop::updateInterestState(int32_t fd,
                                    bool needDisableRead,
                                    bool needDisableWrite) noexcept {
    {
        std::lock_guard<std::mutex> lock(interestMutex_);
        auto it = interestStates_.find(fd);
        if (it != interestStates_.end()) {
            auto& state = it->second;
            if (needDisableRead) {
                state.hasReadWaiter = false;
            }
            if (needDisableWrite) {
                state.hasWriteWaiter = false;
            }
            // 如果没有等待者了，清理状态
            if (!state.hasReadWaiter && !state.hasWriteWaiter) {
                interestStates_.erase(it);
            }
        }
    }

    // 在锁外更新系统事件，避免死锁
    if (needDisableRead) {
        setReadable(fd, false);
    }
    if (needDisableWrite) {
        setWritable(fd, false);
    }
}

void EventLoop::resumeCoroutines(std::coroutine_handle<> readHandle,
                                 std::coroutine_handle<> writeHandle) noexcept {
    if (!running_.load(std::memory_order_acquire)) {
        return;
    }

    if (readHandle && !readHandle.done()) {
        CoroutineScheduler::instance().schedule(readHandle);
    }
    if (writeHandle && !writeHandle.done()) {
        CoroutineScheduler::instance().schedule(writeHandle);
    }
}

void EventLoop::dispatchEvent(int32_t fd, bool readable, bool writable, bool error) noexcept {
    // 1. 提取等待句柄
    auto handles = extractHandles(fd, readable, writable, error);

    // 2. 更新事件关注状态
    updateInterestState(fd, handles.needDisableRead, handles.needDisableWrite);

    // 3. 恢复协程
    resumeCoroutines(handles.readHandle, handles.writeHandle);
}

void EventLoop::poll() noexcept {
#ifdef __linux__
    epoll_event events[kMaxEvents];
    int32_t nfds = epoll_wait(epollFd_, events, kMaxEvents, kPollTimeoutMs);

    for (int32_t i = 0; i < nfds; ++i) {
        int32_t fd = events[i].data.fd;
        // 处理唤醒事件
        if (fd == wakeupFd_) {
            uint64_t value;
            ::read(wakeupFd_, &value, sizeof(value));
            continue;
        }
        uint32_t revents = events[i].events;

        bool isReadable = (revents & EPOLLIN) != 0;
        bool isWritable = (revents & EPOLLOUT) != 0;
        bool hasError = (revents & (EPOLLERR | EPOLLHUP)) != 0;

        dispatchEvent(fd, isReadable, isWritable, hasError);
    }
#elif defined(__APPLE__)
    struct kevent events[kMaxEvents];
    struct timespec timeout = {0, kPollTimeoutMs * 1000000};
    int32_t nfds = kevent(kqueueFd_, nullptr, 0, events, kMaxEvents, &timeout);

    for (int32_t i = 0; i < nfds; ++i) {
        int32_t fd = static_cast<int32_t>(events[i].ident);
        // 处理唤醒事件
        if (fd == 0 && events[i].filter == EVFILT_USER) {
            continue;
        }
        int16_t filter = events[i].filter;
        bool hasError = (events[i].flags & EV_ERROR) != 0;

        bool isReadable = (filter == EVFILT_READ);
        bool isWritable = (filter == EVFILT_WRITE);

        dispatchEvent(fd, isReadable, isWritable, hasError);
    }
#endif
}

} // namespace slark
