// Copyright (c) 2026 Nevermore-. All rights reserved.

#include "Awaitable.hpp"
#include "EventLoop.h"
#include "Task.hpp"

#include <gtest/gtest.h>

#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <memory>
#include <thread>

namespace {

using slark::CompletionAwaiter;
using slark::EventLoop;
using slark::FdInterest;
using slark::ResumeGate;
using slark::Task;

Task<void> suspendedOn(std::shared_ptr<CompletionAwaiter<void>> awaiter) {
    co_await *awaiter;
}

struct LoopFixture {
    EventLoop loop;
    std::thread thread;

    LoopFixture() {
        thread = std::thread([this] { loop.run(); });
        while (!loop.isRunning()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }

    ~LoopFixture() {
        loop.stop();
        thread.join();
    }
};

struct SocketPair {
    int fds[2] = {-1, -1};

    bool open() {
        return ::socketpair(AF_UNIX, SOCK_STREAM, 0, fds) == 0;
    }

    ~SocketPair() {
        close();
    }

    void close() {
        for (auto& fd : fds) {
            if (fd >= 0) {
                ::close(fd);
                fd = -1;
            }
        }
    }
};

std::shared_ptr<ResumeGate> makeGate(Task<void>& task,
                                     std::shared_ptr<CompletionAwaiter<void>> awaiter) {
    task = suspendedOn(std::move(awaiter));
    task.resume();
    return slark::makeResumeGate(task.getHandle());
}

bool waitDone(const Task<void>& task, int32_t timeoutMs = 2000) {
    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    while (!task.isDone() && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return task.isDone();
}

}  // namespace

TEST(EventLoopRaceTest, RegisterCancelRegisterSameFdSameInterest) {
    LoopFixture fx;
    SocketPair sp;
    ASSERT_TRUE(sp.open());
    fx.loop.registerFd(sp.fds[0]);

    auto awaiter1 = std::make_shared<CompletionAwaiter<void>>();
    Task<void> task1;
    auto gate1 = makeGate(task1, awaiter1);
    auto reg1 = fx.loop.registerContinuation(sp.fds[0], FdInterest::Read, gate1);
    fx.loop.setReadable(sp.fds[0], true);
    gate1->arm();
    reg1.cancel();

    auto awaiter2 = std::make_shared<CompletionAwaiter<void>>();
    Task<void> task2;
    auto gate2 = makeGate(task2, awaiter2);
    auto reg2 = fx.loop.registerContinuation(sp.fds[0], FdInterest::Read, gate2);
    fx.loop.setReadable(sp.fds[0], true);
    gate2->arm();

    ASSERT_EQ(::write(sp.fds[1], "x", 1), 1);
    EXPECT_TRUE(waitDone(task2)) << "a waiter registered the second time must be woken by incoming data";
    reg2.cancel();
}

TEST(EventLoopRaceTest, UnregisterContinuationWithExpectedGateKeepsNewWaiter) {
    LoopFixture fx;
    SocketPair sp;
    ASSERT_TRUE(sp.open());
    fx.loop.registerFd(sp.fds[0]);

    auto awaiter1 = std::make_shared<CompletionAwaiter<void>>();
    Task<void> task1;
    auto gate1 = makeGate(task1, awaiter1);
    auto reg1 = fx.loop.registerContinuation(sp.fds[0], FdInterest::Read, gate1);
    fx.loop.setReadable(sp.fds[0], true);
    gate1->arm();
    fx.loop.unregisterContinuation(sp.fds[0], FdInterest::Read, gate1);

    auto awaiter2 = std::make_shared<CompletionAwaiter<void>>();
    Task<void> task2;
    auto gate2 = makeGate(task2, awaiter2);
    auto reg2 = fx.loop.registerContinuation(sp.fds[0], FdInterest::Read, gate2);
    fx.loop.setReadable(sp.fds[0], true);
    gate2->arm();

    ASSERT_EQ(::write(sp.fds[1], "y", 1), 1);
    EXPECT_TRUE(waitDone(task2)) << "a new waiter must still wake after expectedGate is unregistered";
    reg2.cancel();
}

TEST(EventLoopRaceTest, FdCloseThenReuseDoesNotSignalOldWaiter) {
    LoopFixture fx;
    SocketPair sp;
    ASSERT_TRUE(sp.open());
    int32_t fdToReuse = sp.fds[0];
    fx.loop.registerFd(fdToReuse);

    auto awaiter1 = std::make_shared<CompletionAwaiter<void>>();
    Task<void> task1;
    auto gate1 = makeGate(task1, awaiter1);
    auto reg1 = fx.loop.registerContinuation(fdToReuse, FdInterest::Read, gate1);
    fx.loop.setReadable(fdToReuse, true);
    gate1->arm();

    fx.loop.unregisterContinuation(fdToReuse, FdInterest::Read, gate1);
    sp.close();

    SocketPair sp2;
    ASSERT_TRUE(sp2.open());
    if (sp2.fds[0] != fdToReuse) {
        return;
    }
    fx.loop.registerFd(fdToReuse);

    auto awaiter2 = std::make_shared<CompletionAwaiter<void>>();
    Task<void> task2;
    auto gate2 = makeGate(task2, awaiter2);
    auto reg2 = fx.loop.registerContinuation(fdToReuse, FdInterest::Read, gate2);
    fx.loop.setReadable(fdToReuse, true);
    gate2->arm();

    ASSERT_EQ(::write(sp2.fds[1], "z", 1), 1);
    EXPECT_TRUE(waitDone(task2)) << "a new waiter must wake normally after the fd is reused";
    reg2.cancel();
}

TEST(EventLoopRaceTest, DispatchAndRegisterInterleaving) {
    LoopFixture fx;
    SocketPair sp;
    ASSERT_TRUE(sp.open());
    fx.loop.registerFd(sp.fds[0]);

    std::atomic<bool> stop{false};
    std::thread registrar([&] {
        auto awaiter = std::make_shared<CompletionAwaiter<void>>();
        Task<void> task;
        auto gate = makeGate(task, awaiter);
        auto reg = fx.loop.registerContinuation(sp.fds[0], FdInterest::Read, gate);
        fx.loop.setReadable(sp.fds[0], true);
        gate->arm();
        while (!stop.load()) {
            std::this_thread::sleep_for(std::chrono::microseconds(50));
        }
        reg.cancel();
    });

    for (int i = 0; i < 1000; ++i) {
        (void)::write(sp.fds[1], "e", 1);
        std::this_thread::sleep_for(std::chrono::microseconds(100));
    }
    stop.store(true);
    registrar.join();

    auto awaiter = std::make_shared<CompletionAwaiter<void>>();
    Task<void> task;
    auto gate = makeGate(task, awaiter);
    auto reg = fx.loop.registerContinuation(sp.fds[0], FdInterest::Read, gate);
    fx.loop.setReadable(sp.fds[0], true);
    gate->arm();
    ASSERT_EQ(::write(sp.fds[1], "w", 1), 1);
    EXPECT_TRUE(waitDone(task)) << "after interleaved registration, the waiter interest must not be disabled by an old dispatch";
    reg.cancel();
}
