// Copyright (c) 2026 Nevermore-. All rights reserved.

#include <gtest/gtest.h>

#include <atomic>
#include <latch>
#include <memory>
#include <thread>

#include "AsyncGenerator.hpp"
#include "Awaitable.hpp"
#include "CoroutineTestSupport.h"
#include "Task.hpp"

using slark::AsyncGenerator;
using slark::CompletionAwaiter;
using slark::Task;

namespace {

struct LiveFrames {
    std::atomic<int> alive{0};
};

struct FrameGuard {
    explicit FrameGuard(LiveFrames* owner) : frames(owner) {
        frames->alive.fetch_add(1);
    }

    ~FrameGuard() {
        frames->alive.fetch_sub(1);
    }

    FrameGuard(const FrameGuard&) = delete;
    FrameGuard& operator=(const FrameGuard&) = delete;

    LiveFrames* frames;
};

using Gate = std::shared_ptr<CompletionAwaiter<void>>;

Task<void> parkOn(Gate gate, LiveFrames* frames) {
    FrameGuard guard{frames};
    co_await *gate;
}

Task<void> parentOf(Task<void> child, LiveFrames* frames, std::atomic<bool>* ranAfterChild) {
    FrameGuard guard{frames};
    co_await std::move(child);
    ranAfterChild->store(true);
}

AsyncGenerator<int> producerAwaiting(Gate gate, LiveFrames* frames) {
    FrameGuard guard{frames};
    co_await parkOn(std::move(gate), frames);
    co_yield 1;
}

Task<void> pullOnce(AsyncGenerator<int>* generator) {
    (void)co_await generator->next();
}

}  // namespace

TEST(ChainDestroyTest, DestroyingTheRootOfANestedChainReleasesEveryFrame) {
    LiveFrames frames;
    std::atomic<bool> ran{false};
    auto gate = std::make_shared<CompletionAwaiter<void>>();
    {
        auto root = parentOf(parentOf(parkOn(gate, &frames), &frames, &ran), &frames, &ran);
        root.resume();
        ASSERT_FALSE(root.isDone());
        ASSERT_EQ(frames.alive.load(), 3);
    }
    EXPECT_EQ(frames.alive.load(), 0) << "a chain parked on a leaf must not outlive its root";
}

TEST(ChainDestroyTest, ADestroyedParentNeverRunsAfterItsChildFinishes) {
    LiveFrames frames;
    std::atomic<bool> ran{false};
    auto gate = std::make_shared<CompletionAwaiter<void>>();
    {
        auto root = parentOf(parkOn(gate, &frames), &frames, &ran);
        root.resume();
        ASSERT_EQ(frames.alive.load(), 2);
    }
    gate->complete();
    EXPECT_FALSE(ran.load()) << "an abandoned parent must not continue its body";
    EXPECT_EQ(frames.alive.load(), 0);
}

TEST(ChainDestroyTest, ClosingAGeneratorThatAwaitsAParkedChildReleasesTheChain) {
    LiveFrames frames;
    auto gate = std::make_shared<CompletionAwaiter<void>>();
    {
        auto generator = producerAwaiting(gate, &frames);
        auto consumer = pullOnce(&generator);
        consumer.resume();
        ASSERT_FALSE(consumer.isDone());
        ASSERT_EQ(frames.alive.load(), 2);
    }
    EXPECT_EQ(frames.alive.load(), 0) << "closing the generator must take its parked child with it";
}

TEST(ChainDestroyTest, DestroyRacingChildCompletionReleasesEveryFrame) {
    for (int round = 0; round < 500; ++round) {
        LiveFrames frames;
        std::atomic<bool> ran{false};
        auto gate = std::make_shared<CompletionAwaiter<void>>();
        auto root = parentOf(parkOn(gate, &frames), &frames, &ran);
        root.resume();
        ASSERT_EQ(frames.alive.load(), 2);

        std::latch start(2);
        std::thread completer([&] {
            start.arrive_and_wait();
            gate->complete();
        });
        std::thread destroyer([&] {
            start.arrive_and_wait();
            root = Task<void>{};
        });
        completer.join();
        destroyer.join();
        ASSERT_EQ(frames.alive.load(), 0) << "round " << round;
    }
}
