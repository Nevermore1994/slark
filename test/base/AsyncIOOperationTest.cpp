//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <latch>
#include <memory>
#include <optional>
#include <string>
#include <thread>

#include "AsyncIOOperation.hpp"
#include "CoroutineTestSupport.h"
#include "Executor.h"
#include "Task.hpp"

using namespace std::chrono_literals;
using slark::Error;
using slark::IOResult;
using slark::SerialExecutor;
using slark::Task;
using slark::test::ManualIO;
using slark::test::waitUntilDone;

namespace {

Task<IOResult<std::string>> awaitManual(std::shared_ptr<ManualIO> io, std::atomic<int>* resumes) {
    try {
        auto outcome = co_await *io;
        resumes->fetch_add(1, std::memory_order_relaxed);
        co_return outcome;
    } catch (const slark::CancelledException&) {
        resumes->fetch_add(1, std::memory_order_relaxed);
        throw;
    }
}

}  // namespace

TEST(AsyncIOOperationTest, CancelRacingSetCancellationTokenNeverLosesTheWake) {
    int lost = 0;
    for (int round = 0; round < 2000; ++round) {
        auto io = std::make_shared<ManualIO>();
        std::atomic<int> resumes{0};
        slark::CancellationToken token;
        auto task = awaitManual(io, &resumes);

        std::latch start(2);
        std::thread binder([&] {
            start.arrive_and_wait();
            io->setCancellationToken(token);
        });
        std::thread canceller([&] {
            start.arrive_and_wait();
            token.cancel();
        });
        binder.join();
        canceller.join();

        task.resume();
        if (!task.isDone()) {
            const auto deadline = std::chrono::steady_clock::now() + 50ms;
            while (!task.isDone() && std::chrono::steady_clock::now() < deadline) {
                std::this_thread::sleep_for(1ms);
            }
        }
        if (!task.isDone() && token.isCancelled()) {
            ++lost;
        }
    }
    EXPECT_EQ(lost, 0) << "a cancel that lands while the token is being bound must still wake the wait";
}

TEST(AsyncIOOperationTest, CompleteRacingFailHasExactlyOneOutcome) {
    for (int round = 0; round < 200; ++round) {
        auto io = std::make_shared<ManualIO>();
        std::atomic<int> resumes{0};
        auto task = awaitManual(io, &resumes);
        task.resume();
        ASSERT_FALSE(task.isDone()) << "round " << round;

        std::latch start(2);
        std::thread completer([&] {
            start.arrive_and_wait();
            io->finish("value");
        });
        std::thread failer([&] {
            start.arrive_and_wait();
            io->abort(Error::NetworkError);
        });
        completer.join();
        failer.join();

        ASSERT_TRUE(waitUntilDone(task)) << "round " << round;
        EXPECT_EQ(resumes.load(), 1) << "round " << round;
        const auto outcome = task.result();
        if (outcome.has_value()) {
            EXPECT_EQ(outcome.value(), "value") << "round " << round;
        } else {
            EXPECT_EQ(outcome.error(), Error::NetworkError) << "round " << round;
        }
    }
}

TEST(AsyncIOOperationTest, CompleteRacingTimeoutHasExactlyOneOutcome) {
    for (int round = 0; round < 100; ++round) {
        auto io = std::make_shared<ManualIO>();
        io->setTimeout(1ms);
        std::atomic<int> resumes{0};
        auto task = awaitManual(io, &resumes);
        task.resume();

        std::thread completer([&] {
            std::this_thread::sleep_for(1ms);
            io->finish("value");
        });
        completer.join();

        ASSERT_TRUE(waitUntilDone(task)) << "round " << round;
        EXPECT_EQ(resumes.load(), 1) << "round " << round;
        const auto outcome = task.result();
        if (outcome.has_value()) {
            EXPECT_EQ(outcome.value(), "value") << "round " << round;
        } else {
            EXPECT_EQ(outcome.error(), Error::ConnectionTimeout) << "round " << round;
        }
    }
}

TEST(AsyncIOOperationTest, ResultCompletedOnAnotherThreadIsReadByTheWaiter) {
    const std::string payload(4096, 'x');
    auto executor = std::make_shared<SerialExecutor>();
    for (int round = 0; round < 100; ++round) {
        auto io = std::make_shared<ManualIO>();
        std::atomic<int> resumes{0};
        auto task = awaitManual(io, &resumes);
        task.setExecutor(executor);
        task.resume();

        std::thread completer([&] {
            io->finish(payload);
        });
        completer.join();

        ASSERT_TRUE(waitUntilDone(task)) << "round " << round;
        EXPECT_EQ(resumes.load(), 1) << "round " << round;
        const auto outcome = task.result();
        ASSERT_TRUE(outcome.has_value()) << "round " << round;
        EXPECT_EQ(outcome.value(), payload) << "round " << round;
    }
}

TEST(AsyncIOOperationTest, ErrorCompletedOnAnotherThreadIsReadByTheWaiter) {
    auto executor = std::make_shared<SerialExecutor>();
    for (int round = 0; round < 100; ++round) {
        auto io = std::make_shared<ManualIO>();
        std::atomic<int> resumes{0};
        auto task = awaitManual(io, &resumes);
        task.setExecutor(executor);
        task.resume();

        std::thread completer([&] {
            io->abort(Error::ConnectionReset);
        });
        completer.join();

        ASSERT_TRUE(waitUntilDone(task)) << "round " << round;
        EXPECT_EQ(resumes.load(), 1) << "round " << round;
        const auto outcome = task.result();
        ASSERT_FALSE(outcome.has_value()) << "round " << round;
        EXPECT_EQ(outcome.error(), Error::ConnectionReset) << "round " << round;
    }
}

TEST(AsyncIOOperationTest, FailRacingCancelHasExactlyOneOutcome) {
    for (int round = 0; round < 200; ++round) {
        auto io = std::make_shared<ManualIO>();
        std::atomic<int> resumes{0};
        slark::CancellationToken token;
        auto task = awaitManual(io, &resumes);
        task.setCancellationToken(token);
        task.resume();
        ASSERT_FALSE(task.isDone()) << "round " << round;

        std::atomic<bool> go{false};
        std::thread failer([&] {
            while (!go.load()) {
                std::this_thread::yield();
            }
            io->abort(Error::NetworkError);
        });
        std::thread canceller([&] {
            while (!go.load()) {
                std::this_thread::yield();
            }
            token.cancel();
        });
        go.store(true);
        failer.join();
        canceller.join();

        ASSERT_TRUE(waitUntilDone(task)) << "round " << round;
        EXPECT_EQ(resumes.load(), 1) << "round " << round;
        // Exactly one winner: a failure is a value, a cancellation is an exception.
        std::optional<Error> failure;
        bool cancelled = false;
        try {
            const auto outcome = task.result();
            ASSERT_FALSE(outcome.has_value()) << "round " << round;
            failure = outcome.error();
        } catch (const slark::CancelledException&) {
            cancelled = true;
        }
        EXPECT_NE(cancelled, failure.has_value()) << "round " << round;
        if (failure) {
            EXPECT_EQ(*failure, Error::NetworkError) << "round " << round;
        }
    }
}
