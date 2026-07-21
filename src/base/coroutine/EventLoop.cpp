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
    wakeup();
}

void EventLoop::wakeup() noexcept {
#ifdef __linux__
    if (wakeupFd_ >= 0) {
        uint64_t value = 1;
        (void)::write(wakeupFd_, &value, sizeof(value));
    }
#elif defined(__APPLE__)
    if (kqueueFd_ >= 0) {
        struct kevent change;
        EV_SET(&change, 0, EVFILT_USER, EV_ADD | EV_ENABLE, NOTE_TRIGGER, 0, nullptr);
        (void)kevent(kqueueFd_, &change, 1, nullptr, 0, nullptr);
    }
#endif
}

void EventLoop::registerFd(int32_t fd) noexcept {
    int32_t flags = fcntl(fd, F_GETFL, 0);
    if (flags >= 0) {
        (void)fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    }

#ifdef __linux__
    epoll_event event{};
    event.events = EPOLLET;
    event.data.fd = fd;
    (void)epoll_ctl(epollFd_, EPOLL_CTL_ADD, fd, &event);
#endif
}

void EventLoop::unregisterFd(int32_t fd) noexcept {
    unregisterAllContinuations(fd);

#ifdef __linux__
    {
        std::lock_guard<std::mutex> lock(fdMutex_);
        fdEvents_.erase(fd);
    }
    (void)epoll_ctl(epollFd_, EPOLL_CTL_DEL, fd, nullptr);
#elif defined(__APPLE__)
    struct kevent changes[2];
    EV_SET(&changes[0], fd, EVFILT_READ, EV_DELETE, 0, 0, nullptr);
    EV_SET(&changes[1], fd, EVFILT_WRITE, EV_DELETE, 0, 0, nullptr);
    (void)kevent(kqueueFd_, changes, 2, nullptr, 0, nullptr);
#endif
}

void EventLoop::setReadable(int32_t fd, bool enabled) noexcept {
#ifdef __linux__
    std::lock_guard<std::mutex> lock(fdMutex_);
    if (enabled) {
        fdEvents_[fd] |= EPOLLIN;
    } else {
        fdEvents_[fd] &= ~EPOLLIN;
    }
    epoll_event event{};
    event.data.fd = fd;
    event.events = EPOLLET | fdEvents_[fd];
    (void)epoll_ctl(epollFd_, EPOLL_CTL_MOD, fd, &event);
#elif defined(__APPLE__)
    struct kevent change;
    EV_SET(&change, fd, EVFILT_READ, enabled ? EV_ADD | EV_ENABLE : EV_DELETE, 0, 0, nullptr);
    (void)kevent(kqueueFd_, &change, 1, nullptr, 0, nullptr);
#endif
}

void EventLoop::setWritable(int32_t fd, bool enabled) noexcept {
#ifdef __linux__
    std::lock_guard<std::mutex> lock(fdMutex_);
    if (enabled) {
        fdEvents_[fd] |= EPOLLOUT;
    } else {
        fdEvents_[fd] &= ~EPOLLOUT;
    }
    epoll_event event{};
    event.data.fd = fd;
    event.events = EPOLLET | fdEvents_[fd];
    (void)epoll_ctl(epollFd_, EPOLL_CTL_MOD, fd, &event);
#elif defined(__APPLE__)
    struct kevent change;
    EV_SET(&change, fd, EVFILT_WRITE, enabled ? EV_ADD | EV_ENABLE : EV_DELETE, 0, 0, nullptr);
    (void)kevent(kqueueFd_, &change, 1, nullptr, 0, nullptr);
#endif
}

void EventLoop::RegistrationToken::cancel() noexcept {
    auto loop = std::exchange(loop_, nullptr);
    auto handle = std::exchange(handle_, nullptr);
    if (!loop || !handle) {
        return;
    }
    bool needDisable = false;
    auto removed = loop->takeContinuation(fd_, interest_, needDisable, handle);
    if (needDisable) {
        if (interest_ == FdInterest::Read) {
            loop->setReadable(fd_, false);
        } else {
            loop->setWritable(fd_, false);
        }
    }
    if (removed && removed->notify()) {
        CoroutineScheduler::instance().schedule(std::move(removed));
    }
}

EventLoop::RegistrationToken EventLoop::registerContinuation(
    int32_t fd,
    FdInterest interest,
    std::shared_ptr<SharedCoroutineHandle> handle) noexcept {
    if (!handle) {
        return {};
    }

    {
        std::lock_guard<std::mutex> lock(fdMutex_);
        auto& waiters = continuations_[fd];
        auto& slot = interest == FdInterest::Read ? waiters.readHandle : waiters.writeHandle;
        assert((!slot || slot->done()) && "Overwriting pending fd waiter");
        if (slot && !slot->done()) {
            LogW("EventLoop: overwriting pending waiter on fd={}", fd);
        }
        slot = handle;
    }

    return RegistrationToken(this, std::move(handle), fd, interest);
}

std::shared_ptr<SharedCoroutineHandle> EventLoop::takeContinuation(
    int32_t fd,
    FdInterest interest,
    bool& needDisable,
    const std::shared_ptr<SharedCoroutineHandle>& expected) noexcept {
    needDisable = false;
    std::lock_guard<std::mutex> lock(fdMutex_);
    auto it = continuations_.find(fd);
    if (it == continuations_.end()) {
        return nullptr;
    }

    auto& slot = interest == FdInterest::Read ? it->second.readHandle : it->second.writeHandle;
    if (expected && slot != expected) {
        return nullptr;
    }
    auto handle = std::move(slot);
    needDisable = handle != nullptr;
    if (it->second.empty()) {
        continuations_.erase(it);
    }
    return handle;
}

void EventLoop::unregisterContinuation(int32_t fd, FdInterest interest) noexcept {
    bool needDisable = false;
    (void)takeContinuation(fd, interest, needDisable);
    if (needDisable) {
        if (interest == FdInterest::Read) {
            setReadable(fd, false);
        } else {
            setWritable(fd, false);
        }
    }
}

void EventLoop::unregisterAllContinuations(int32_t fd) noexcept {
    bool disableRead = false;
    bool disableWrite = false;
    {
        std::lock_guard<std::mutex> lock(fdMutex_);
        auto it = continuations_.find(fd);
        if (it == continuations_.end()) {
            return;
        }
        disableRead = it->second.readHandle != nullptr;
        disableWrite = it->second.writeHandle != nullptr;
        continuations_.erase(it);
    }
    if (disableRead) {
        setReadable(fd, false);
    }
    if (disableWrite) {
        setWritable(fd, false);
    }
}

void EventLoop::dispatchEvent(int32_t fd, bool readable, bool writable, bool error) noexcept {
    std::shared_ptr<SharedCoroutineHandle> readHandle;
    std::shared_ptr<SharedCoroutineHandle> writeHandle;
    bool disableRead = false;
    bool disableWrite = false;

    if (readable || error) {
        readHandle = takeContinuation(fd, FdInterest::Read, disableRead);
    }
    if (writable || error) {
        writeHandle = takeContinuation(fd, FdInterest::Write, disableWrite);
    }

    if (disableRead) {
        setReadable(fd, false);
    }
    if (disableWrite) {
        setWritable(fd, false);
    }

    if (!running_.load(std::memory_order_acquire)) {
        return;
    }
    if (readHandle && readHandle->notify()) {
        CoroutineScheduler::instance().schedule(std::move(readHandle));
    }
    if (writeHandle && writeHandle->notify()) {
        CoroutineScheduler::instance().schedule(std::move(writeHandle));
    }
}

void EventLoop::poll() noexcept {
#ifdef __linux__
    epoll_event events[kMaxEvents];
    int32_t count = epoll_wait(epollFd_, events, kMaxEvents, kPollTimeoutMs);
    for (int32_t index = 0; index < count; ++index) {
        int32_t fd = events[index].data.fd;
        if (fd == wakeupFd_) {
            uint64_t value = 0;
            (void)::read(wakeupFd_, &value, sizeof(value));
            continue;
        }
        const uint32_t eventsMask = events[index].events;
        dispatchEvent(fd,
                      (eventsMask & EPOLLIN) != 0,
                      (eventsMask & EPOLLOUT) != 0,
                      (eventsMask & (EPOLLERR | EPOLLHUP)) != 0);
    }
#elif defined(__APPLE__)
    struct kevent events[kMaxEvents];
    struct timespec timeout = {0, kPollTimeoutMs * 1000000};
    int32_t count = kevent(kqueueFd_, nullptr, 0, events, kMaxEvents, &timeout);
    for (int32_t index = 0; index < count; ++index) {
        int32_t fd = static_cast<int32_t>(events[index].ident);
        if (fd == 0 && events[index].filter == EVFILT_USER) {
            continue;
        }
        dispatchEvent(fd,
                      events[index].filter == EVFILT_READ,
                      events[index].filter == EVFILT_WRITE,
                      (events[index].flags & EV_ERROR) != 0);
    }
#endif
}

} // namespace slark
