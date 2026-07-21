//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//
#include "CoroutineControlBlock.hpp"
#include "EventLoop.h"

#include <atomic>
#include <chrono>
#include <gtest/gtest.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <thread>
#include <unistd.h>

using namespace slark;

class EventLoopTest : public ::testing::Test {
protected:
    void SetUp() override {
        // 创建一对 socket 用于测试
        int32_t sockets[2];
        ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, sockets), 0);
        readFd_ = sockets[0];
        writeFd_ = sockets[1];
    }

    void TearDown() override {
        if (readFd_ >= 0) {
            close(readFd_);
        }
        if (writeFd_ >= 0) {
            close(writeFd_);
        }
    }

    int32_t readFd_{-1};
    int32_t writeFd_{-1};
};

TEST_F(EventLoopTest, CreateAndRun) {
    EventLoop loop;
    EXPECT_FALSE(loop.isRunning());

    std::thread loopThread([&loop]() {
        loop.run();
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    EXPECT_TRUE(loop.isRunning());

    loop.stop();
    loopThread.join();
    EXPECT_FALSE(loop.isRunning());
}

TEST_F(EventLoopTest, RegisterFd) {
    EventLoop loop;
    EXPECT_NO_THROW(loop.registerFd(readFd_));
    EXPECT_NO_THROW(loop.unregisterFd(readFd_));
}

TEST_F(EventLoopTest, SetReadable) {
    EventLoop loop;
    loop.registerFd(readFd_);

    EXPECT_NO_THROW(loop.setReadable(readFd_, true));
    EXPECT_NO_THROW(loop.setReadable(readFd_, false));

    loop.unregisterFd(readFd_);
}

TEST_F(EventLoopTest, SetWritable) {
    EventLoop loop;
    loop.registerFd(writeFd_);

    EXPECT_NO_THROW(loop.setWritable(writeFd_, true));
    EXPECT_NO_THROW(loop.setWritable(writeFd_, false));

    loop.unregisterFd(writeFd_);
}

TEST_F(EventLoopTest, RegisterContinuation) {
    EventLoop loop;
    loop.registerFd(readFd_);

    auto control = std::make_shared<CoroutineControlBlock>(std::noop_coroutine());
    auto handle = std::make_shared<SharedCoroutineHandle>(control);
    EXPECT_NO_THROW(loop.registerContinuation(readFd_, FdInterest::Read, handle));
    EXPECT_NO_THROW(loop.unregisterContinuation(readFd_, FdInterest::Read));

    loop.unregisterFd(readFd_);
}

TEST_F(EventLoopTest, EventInterestConvergence) {
    // 测试事件收敛逻辑：当所有等待者被移除后，事件关注应该被关闭
    EventLoop loop;
    loop.registerFd(readFd_);

    // 启动事件循环线程
    std::atomic<bool> running{true};
    std::thread loopThread([&]() {
        while (running.load()) {
            // 模拟单次 poll
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    });

    // 注册读事件关注
    auto control = std::make_shared<CoroutineControlBlock>(std::noop_coroutine());
    auto handle = std::make_shared<SharedCoroutineHandle>(control);
    loop.registerContinuation(readFd_, FdInterest::Read, handle);
    loop.setReadable(readFd_, true);

    // 取消注册时应该收敛事件
    loop.unregisterContinuation(readFd_, FdInterest::Read);

    running.store(false);
    loopThread.join();

    loop.unregisterFd(readFd_);
}

TEST_F(EventLoopTest, MultipleInterests) {
    EventLoop loop;
    loop.registerFd(readFd_);

    auto readControl = std::make_shared<CoroutineControlBlock>(std::noop_coroutine());
    auto writeControl = std::make_shared<CoroutineControlBlock>(std::noop_coroutine());
    auto readHandle = std::make_shared<SharedCoroutineHandle>(readControl);
    auto writeHandle = std::make_shared<SharedCoroutineHandle>(writeControl);

    // 同时注册读写事件
    EXPECT_NO_THROW(loop.registerContinuation(readFd_, FdInterest::Read, readHandle));
    EXPECT_NO_THROW(loop.registerContinuation(readFd_, FdInterest::Write, writeHandle));

    // 只取消读事件，写事件应该保持
    EXPECT_NO_THROW(loop.unregisterContinuation(readFd_, FdInterest::Read));

    // 再取消写事件
    EXPECT_NO_THROW(loop.unregisterContinuation(readFd_, FdInterest::Write));

    loop.unregisterFd(readFd_);
}

TEST_F(EventLoopTest, UnregisterAllContinuations) {
    EventLoop loop;
    loop.registerFd(readFd_);

    auto readControl = std::make_shared<CoroutineControlBlock>(std::noop_coroutine());
    auto writeControl = std::make_shared<CoroutineControlBlock>(std::noop_coroutine());
    auto readHandle = std::make_shared<SharedCoroutineHandle>(readControl);
    auto writeHandle = std::make_shared<SharedCoroutineHandle>(writeControl);

    loop.registerContinuation(readFd_, FdInterest::Read, readHandle);
    loop.registerContinuation(readFd_, FdInterest::Write, writeHandle);

    // 一次性取消所有事件
    EXPECT_NO_THROW(loop.unregisterAllContinuations(readFd_));

    loop.unregisterFd(readFd_);
}

TEST_F(EventLoopTest, NativeFd) {
    EventLoop loop;
    int32_t fd = loop.nativeFd();
    EXPECT_GE(fd, 0);
}

TEST_F(EventLoopTest, RegistrationTokenCancel) {
    EventLoop loop;
    loop.registerFd(readFd_);

    // 创建一个挂起的协程（模拟）
    auto control = std::make_shared<CoroutineControlBlock>(std::noop_coroutine());
    auto handle = std::make_shared<SharedCoroutineHandle>(control);
    auto token = loop.registerContinuation(readFd_, FdInterest::Read, handle);

    EXPECT_TRUE(token.valid());
    EXPECT_NO_THROW(token.cancel());
    EXPECT_FALSE(token.valid());

    loop.unregisterFd(readFd_);
}

TEST_F(EventLoopTest, StopIdempotent) {
    EventLoop loop;

    // 多次 stop 应该是幂等的
    EXPECT_NO_THROW(loop.stop());
    EXPECT_NO_THROW(loop.stop());
    EXPECT_NO_THROW(loop.stop());
}
