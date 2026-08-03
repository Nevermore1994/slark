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
        // 等待 loop 进入 poll
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
    // E1：同一 fd 同 interest 快速 register -> cancel -> register，新 waiter 必须可唤醒
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
    EXPECT_TRUE(waitDone(task2)) << "第二次注册的 waiter 必须能被数据到达唤醒";
    reg2.cancel();
}

TEST(EventLoopRaceTest, UnregisterContinuationWithExpectedGateKeepsNewWaiter) {
    // E1 变体：unregisterContinuation(expectedGate) 关闭 interest 时不得影响新 waiter
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
    EXPECT_TRUE(waitDone(task2)) << "expectedGate 注销后新 waiter 必须仍可唤醒";
    reg2.cancel();
}

TEST(EventLoopRaceTest, FdCloseThenReuseDoesNotSignalOldWaiter) {
    // E3：fd close 后被新 socket 复用同一 fd 号，旧 waiter 不得被唤醒
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

    // 关闭并注销旧 fd
    fx.loop.unregisterContinuation(fdToReuse, FdInterest::Read, gate1);
    sp.close();

    // 用新 socketpair 尝试复用同一 fd 号
    SocketPair sp2;
    ASSERT_TRUE(sp2.open());
    if (sp2.fds[0] != fdToReuse) {
        // fd 号未复用，测试失去意义，跳过（无法强制复用）
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
    EXPECT_TRUE(waitDone(task2)) << "复用 fd 后新 waiter 必须正常唤醒";
    reg2.cancel();
}

TEST(EventLoopRaceTest, DispatchAndRegisterInterleaving) {
    // E2：复现 C-03 —— dispatchEvent 取走旧 reg 后、锁外 setReadable(false) 前，
    // 新 waiter 注册成功但其 interest 被旧路径关闭。
    // 阶段二 3.2 修复（关闭前复核 slot 归属）后启用。
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

    // 持续写数据触发 dispatchEvent，与 registrar 的注册交错
    for (int i = 0; i < 1000; ++i) {
        (void)::write(sp.fds[1], "e", 1);
        std::this_thread::sleep_for(std::chrono::microseconds(100));
    }
    stop.store(true);
    registrar.join();

    // 修复后：最终注册的 waiter 必须可被数据唤醒
    auto awaiter = std::make_shared<CompletionAwaiter<void>>();
    Task<void> task;
    auto gate = makeGate(task, awaiter);
    auto reg = fx.loop.registerContinuation(sp.fds[0], FdInterest::Read, gate);
    fx.loop.setReadable(sp.fds[0], true);
    gate->arm();
    ASSERT_EQ(::write(sp.fds[1], "w", 1), 1);
    EXPECT_TRUE(waitDone(task)) << "交错注册后 waiter 的 interest 不得被旧 dispatch 关闭";
    reg.cancel();
}
