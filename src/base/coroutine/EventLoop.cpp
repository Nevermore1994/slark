// Copyright (c) 2026 Nevermore-. All rights reserved.

#include "EventLoop.h"

#include <fcntl.h>

#include <cassert>

#include "Log.hpp"
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
    // stop() may have run before this thread (process exit while event thread not yet started).
    // Must re-check after store: if stop() finished before the store, running_ would be overwritten
    // to true here and the wakeup event already consumed, causing poll to never exit and join() to hang.
    if (stopRequested_.load(std::memory_order_acquire)) {
        running_.store(false, std::memory_order_release);
        return;
    }
    while (running_.load()) {
        poll();
    }
    running_.store(false, std::memory_order_release);
}

void EventLoop::stop() noexcept {
    stopRequested_.store(true, std::memory_order_release);
    running_.store(false);
    wakeup();
}

void EventLoop::wakeup() noexcept {
#ifdef __linux__
    if (wakeupFd_ >= 0) {
        uint64_t v = 1;
        (void)::write(wakeupFd_, &v, sizeof(v));
    }
#elif defined(__APPLE__)
    if (kqueueFd_ >= 0) {
        struct kevent c;
        EV_SET(&c, 0, EVFILT_USER, EV_ADD | EV_ENABLE, NOTE_TRIGGER, 0, 0);
        kevent(kqueueFd_, &c, 1, 0, 0, 0);
    }
#endif
}

void EventLoop::registerFd(int32_t fd) noexcept {
    int32_t flags = fcntl(fd, F_GETFL, 0);
    if (flags >= 0) {
        fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    }
#ifdef __linux__
    epoll_event e{};
    e.events = EPOLLET;
    e.data.fd = fd;
    epoll_ctl(epollFd_, EPOLL_CTL_ADD, fd, &e);
#endif
}

void EventLoop::unregisterFd(int32_t fd) noexcept {
    {
        std::lock_guard l(fdMutex_);
        auto it = fdSlots_.find(fd);
        if (it != fdSlots_.end()) {
            if (it->second.hasRead && it->second.readReg.gate) {
                it->second.readReg.gate->disarm();
            }
            if (it->second.hasWrite && it->second.writeReg.gate) {
                it->second.writeReg.gate->disarm();
            }
            for (auto t = tokenMap_.begin(); t != tokenMap_.end();) {
                if (t->second.first == fd) {
                    tokenMap_.erase(t++);
                } else {
                    ++t;
                }
            }
            fdSlots_.erase(it);
        }
#ifdef __linux__
        fdEvents_.erase(fd);
#endif
    }
#ifdef __linux__
    epoll_ctl(epollFd_, EPOLL_CTL_DEL, fd, nullptr);
#elif defined(__APPLE__)
    struct kevent cs[2];
    EV_SET(&cs[0], fd, EVFILT_READ, EV_DELETE, 0, 0, 0);
    EV_SET(&cs[1], fd, EVFILT_WRITE, EV_DELETE, 0, 0, 0);
    kevent(kqueueFd_, cs, 2, 0, 0, 0);
#endif
}

void EventLoop::setReadable(int32_t fd, bool en) noexcept {
#ifdef __linux__
    std::lock_guard l(fdMutex_);
    if (!en) {
        // C-03: re-check the slot before disabling interest. If a new waiter was registered after
        // dispatch/cancel removed the old one, keep interest to avoid disabling the new waiter.
        auto it = fdSlots_.find(fd);
        if (it != fdSlots_.end() && it->second.hasRead) {
            return;
        }
    }
    if (en) {
        fdEvents_[fd] |= EPOLLIN;
    } else {
        fdEvents_[fd] &= ~EPOLLIN;
    }
    epoll_event e{};
    e.data.fd = fd;
    e.events = EPOLLET | fdEvents_[fd];
    epoll_ctl(epollFd_, EPOLL_CTL_MOD, fd, &e);
#elif defined(__APPLE__)
    std::lock_guard l(fdMutex_);
    if (!en) {
        // C-03: same as above, re-check slot ownership before disabling
        auto it = fdSlots_.find(fd);
        if (it != fdSlots_.end() && it->second.hasRead) {
            return;
        }
    }
    struct kevent c;
    EV_SET(&c, fd, EVFILT_READ, en ? EV_ADD | EV_ENABLE : EV_DELETE, 0, 0, 0);
    kevent(kqueueFd_, &c, 1, 0, 0, 0);
#endif
}

void EventLoop::setWritable(int32_t fd, bool en) noexcept {
#ifdef __linux__
    std::lock_guard l(fdMutex_);
    if (!en) {
        // C-03: re-check slot ownership before disabling
        auto it = fdSlots_.find(fd);
        if (it != fdSlots_.end() && it->second.hasWrite) {
            return;
        }
    }
    if (en) {
        fdEvents_[fd] |= EPOLLOUT;
    } else {
        fdEvents_[fd] &= ~EPOLLOUT;
    }
    epoll_event e{};
    e.data.fd = fd;
    e.events = EPOLLET | fdEvents_[fd];
    epoll_ctl(epollFd_, EPOLL_CTL_MOD, fd, &e);
#elif defined(__APPLE__)
    std::lock_guard l(fdMutex_);
    if (!en) {
        // C-03: re-check slot ownership before disabling
        auto it = fdSlots_.find(fd);
        if (it != fdSlots_.end() && it->second.hasWrite) {
            return;
        }
    }
    struct kevent c;
    EV_SET(&c, fd, EVFILT_WRITE, en ? EV_ADD | EV_ENABLE : EV_DELETE, 0, 0, 0);
    kevent(kqueueFd_, &c, 1, 0, 0, 0);
#endif
}

EventLoop::RegistrationToken EventLoop::createToken(
    int32_t fd,
    FdInterest interest,
    std::shared_ptr<ResumeGate> gate) noexcept {
    uint64_t id = nextId_.fetch_add(1);
    tokenMap_[id] = {fd, interest};
    return RegistrationToken(this, id, fd, interest, std::move(gate));
}

EventLoop::RegistrationToken EventLoop::registerContinuation(
    int32_t fd,
    FdInterest interest,
    std::shared_ptr<ResumeGate> gate) noexcept {
    std::lock_guard l(fdMutex_);
    auto& slot = fdSlots_[fd];
    auto& target = (interest == FdInterest::Read) ? slot.readReg : slot.writeReg;
    auto& hasTarget = (interest == FdInterest::Read) ? slot.hasRead : slot.hasWrite;
    if (hasTarget) {
        LogW("EventLoop: overwriting pending waiter on fd={}", fd);
        if (target.gate) {
            target.gate->disarm();
        }
        for (auto token = tokenMap_.begin(); token != tokenMap_.end();) {
            if (token->second.first == fd && token->second.second == interest) {
                token = tokenMap_.erase(token);
            } else {
                ++token;
            }
        }
    }
    target.gate = gate;
    hasTarget = true;
    return createToken(fd, interest, std::move(gate));
}

std::pair<EventLoop::FdRegistration, bool> EventLoop::takeRegistration(
    int32_t fd,
    FdInterest interest,
    const std::shared_ptr<ResumeGate>* expectedGate) noexcept {
    FdRegistration reg;
    bool needDisable = false;
    std::lock_guard l(fdMutex_);
    auto slotIt = fdSlots_.find(fd);
    if (slotIt == fdSlots_.end()) {
        return {reg, false};
    }
    auto& hasSlot =
        (interest == FdInterest::Read) ? slotIt->second.hasRead : slotIt->second.hasWrite;
    auto& slotReg =
        (interest == FdInterest::Read) ? slotIt->second.readReg : slotIt->second.writeReg;
    if (!hasSlot) {
        return {reg, false};
    }
    if (expectedGate && slotReg.gate != *expectedGate) {
        return {reg, false};
    }
    reg = slotReg;
    needDisable = true;
    hasSlot = false;
    slotReg = {};
    if (!slotIt->second.hasRead && !slotIt->second.hasWrite) {
        fdSlots_.erase(slotIt);
    }
    return {reg, needDisable};
}

void EventLoop::unregisterContinuation(int32_t fd,
                                        FdInterest interest,
                                        std::shared_ptr<ResumeGate> expectedGate) noexcept {
    auto expected = expectedGate;
    auto [reg, needDisable] = takeRegistration(fd, interest, expected ? &expected : nullptr);
    if (needDisable) {
        (interest == FdInterest::Read) ? setReadable(fd, false) : setWritable(fd, false);
    }
    if (reg.gate) {
        reg.gate->disarm();
    }
}

void EventLoop::RegistrationToken::cancel() noexcept {
    auto loop = std::exchange(loop_, nullptr);
    auto id = std::exchange(id_, 0);
    if (!loop) {
        return;
    }
    if (gate_) {
        gate_->disarm();
    }
    auto expectedGate = gate_;
    auto [reg, needDisable] = loop->takeRegistration(fd_, interest_, &expectedGate);
    if (needDisable) {
        (interest_ == FdInterest::Read) ? loop->setReadable(fd_, false)
                                        : loop->setWritable(fd_, false);
    }
    {
        std::lock_guard l(loop->fdMutex_);
        loop->tokenMap_.erase(id);
    }
}

void EventLoop::dispatchEvent(int32_t fd, bool readable, bool writable, bool error) noexcept {
    FdRegistration readReg, writeReg;
    bool disableRead = false, disableWrite = false;
    if (readable || error) {
        auto r = takeRegistration(fd, FdInterest::Read);
        readReg = r.first;
        disableRead = r.second;
    }
    if (writable || error) {
        auto r = takeRegistration(fd, FdInterest::Write);
        writeReg = r.first;
        disableWrite = r.second;
    }
    if (disableRead) {
        setReadable(fd, false);
    }
    if (disableWrite) {
        setWritable(fd, false);
    }
    if (!running_.load()) {
        if (readReg.gate) {
            readReg.gate->disarm();
        }
        if (writeReg.gate) {
            writeReg.gate->disarm();
        }
        return;
    }
    if (readReg.gate) {
        readReg.gate->signal();
    }
    if (writeReg.gate) {
        writeReg.gate->signal();
    }
}

void EventLoop::poll() noexcept {
#ifdef __linux__
    epoll_event events[kMaxEvents];
    int32_t cnt = epoll_wait(epollFd_, events, kMaxEvents, kPollTimeoutMs);
    for (int32_t i = 0; i < cnt; ++i) {
        int32_t fd = events[i].data.fd;
        if (fd == wakeupFd_) {
            uint64_t v = 0;
            (void)::read(wakeupFd_, &v, sizeof(v));
            continue;
        }
        uint32_t m = events[i].events;
        dispatchEvent(fd, (m & EPOLLIN) != 0, (m & EPOLLOUT) != 0,
                      (m & (EPOLLERR | EPOLLHUP)) != 0);
    }
#elif defined(__APPLE__)
    struct kevent events[kMaxEvents];
    struct timespec ts = {0, kPollTimeoutMs * 1000000};
    int32_t cnt = kevent(kqueueFd_, 0, 0, events, kMaxEvents, &ts);
    for (int32_t i = 0; i < cnt; ++i) {
        int32_t fd = static_cast<int32_t>(events[i].ident);
        if (fd == 0 && events[i].filter == EVFILT_USER) {
            continue;
        }
        dispatchEvent(fd, events[i].filter == EVFILT_READ, events[i].filter == EVFILT_WRITE,
                      (events[i].flags & EV_ERROR) != 0);
    }
#endif
}

}  // namespace slark
