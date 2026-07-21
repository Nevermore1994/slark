//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//
#include "Awaitable.hpp"
#include "Coroutine.hpp"
#include "Log.hpp"

#include <gtest/gtest.h>

using namespace slark;

// 验证 final_suspend 后 promise.completed 标志被正确设置
TEST(CoroutineAbiTest, FinalSuspendCompletedFlag) {
    auto coro = []() -> Coroutine<int> {
        co_return 42;
    }();

    auto h = coro.getHandle();
    auto& p = h.promise();
    LogI("before resume: completed={}", static_cast<int>(p.completed.load()));
    EXPECT_FALSE(p.completed.load());
    EXPECT_FALSE(h.done());

    coro.resume();
    LogI("after resume: completed={} handle.done={}",
         static_cast<int>(p.completed.load()),
         static_cast<int>(h.done()));
    EXPECT_TRUE(coro.isDone());
    EXPECT_TRUE(p.completed);
}

TEST(CoroutineAbiTest, FinalSuspendCompletedFlagVoid) {
    auto coro = []() -> Coroutine<void> {
        co_return;
    }();

    auto h = coro.getHandle();
    auto& p = h.promise();
    LogI("void before resume: completed={}", static_cast<int>(p.completed.load()));
    EXPECT_FALSE(p.completed.load());
    EXPECT_FALSE(h.done());

    coro.resume();
    LogI("void after resume: completed={} handle.done={}",
         static_cast<int>(p.completed.load()),
         static_cast<int>(h.done()));
    EXPECT_TRUE(coro.isDone());
    EXPECT_TRUE(p.completed);
}
