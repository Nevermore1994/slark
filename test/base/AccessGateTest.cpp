//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//
#include "AccessGate.hpp"

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <future>
#include <memory>
#include <thread>

using namespace slark;
using namespace std::chrono_literals;

namespace {

// Threads below are detached and own the shared state, so a regression that blocks
// closeAndWait() forever fails the test on timeout instead of hanging in a join/future dtor.
struct Shared {
    AccessGate gate;
    std::atomic<bool> stop{false};
    std::atomic<int> active{0};
    std::atomic<int> violations{0};
};

template <typename Fn>
std::future<void> runDetached(std::shared_ptr<Shared> shared, Fn fn) {
    auto promise = std::make_shared<std::promise<void>>();
    auto future = promise->get_future();
    std::thread([shared = std::move(shared), promise, fn = std::move(fn)]() mutable {
        fn(*shared);
        promise->set_value();
    }).detach();
    return future;
}

void startReader(const std::shared_ptr<Shared>& shared, bool yield) {
    std::thread([shared, yield] {
        while (!shared->stop.load(std::memory_order_acquire)) {
            AccessGate::Scope scope(&shared->gate);
            if (scope) {
                shared->active.fetch_add(1);
                if (yield) {
                    std::this_thread::yield();
                }
                shared->active.fetch_sub(1);
            }
        }
    }).detach();
}

}  // namespace

TEST(AccessGate, EnterWhenOpen) {
    AccessGate gate;
    AccessGate::Scope scope(&gate);
    EXPECT_TRUE(scope);
}

TEST(AccessGate, RejectWhenInitiallyClosed) {
    AccessGate gate(false);
    EXPECT_TRUE(gate.isClosed());
    AccessGate::Scope scope(&gate);
    EXPECT_FALSE(scope);
    gate.open();
    AccessGate::Scope reopened(&gate);
    EXPECT_TRUE(reopened);
}

TEST(AccessGate, NullGateIsRejected) {
    AccessGate::Scope scope(nullptr);
    EXPECT_FALSE(scope);
}

TEST(AccessGate, CloseWithoutReadersReturnsImmediately) {
    AccessGate gate;
    gate.closeAndWait();
    EXPECT_TRUE(gate.isClosed());
    EXPECT_FALSE(gate.tryEnter());
}

TEST(AccessGate, CloseWaitsForActiveReader) {
    auto shared = std::make_shared<Shared>();
    auto reader = std::make_unique<AccessGate::Scope>(&shared->gate);
    ASSERT_TRUE(*reader);

    auto closer = runDetached(shared, [](Shared& s) {
        s.gate.closeAndWait();
    });

    EXPECT_NE(closer.wait_for(50ms), std::future_status::ready);
    EXPECT_FALSE(shared->gate.tryEnter());

    reader.reset();
    EXPECT_EQ(closer.wait_for(2s), std::future_status::ready);
}

TEST(AccessGate, ReopenAfterClose) {
    AccessGate gate;
    gate.closeAndWait();
    gate.open();
    EXPECT_FALSE(gate.isClosed());
    AccessGate::Scope scope(&gate);
    EXPECT_TRUE(scope);
}

// Regression: leave() notifying without the mutex lost the wakeup when it raced with the
// waiter's predicate check, leaving closeAndWait() blocked forever.
TEST(AccessGate, ConcurrentEnterLeaveAndCloseNeverHangs) {
    constexpr int kRounds = 2000;
    constexpr int kReaders = 4;

    for (int round = 0; round < kRounds; ++round) {
        auto shared = std::make_shared<Shared>();
        for (int i = 0; i < kReaders; ++i) {
            startReader(shared, false);
        }

        auto closer = runDetached(shared, [](Shared& s) {
            s.gate.closeAndWait();
            // Once closeAndWait returns no reader may still be inside.
            if (s.active.load() != 0) {
                s.violations.fetch_add(1);
            }
        });
        const auto status = closer.wait_for(3s);
        shared->stop.store(true, std::memory_order_release);
        ASSERT_EQ(status, std::future_status::ready) << "closeAndWait hung at round " << round;
        EXPECT_EQ(shared->violations.load(), 0);
    }
}

TEST(AccessGate, RepeatedCloseOpenCyclesWithReaders) {
    auto shared = std::make_shared<Shared>();
    startReader(shared, true);

    auto owner = runDetached(shared, [](Shared& s) {
        for (int i = 0; i < 2000; ++i) {
            s.gate.closeAndWait();
            if (s.active.load() != 0) {
                s.violations.fetch_add(1);
            }
            s.gate.open();
        }
    });
    const auto status = owner.wait_for(10s);
    shared->stop.store(true, std::memory_order_release);
    ASSERT_EQ(status, std::future_status::ready) << "closeAndWait hung";
    EXPECT_EQ(shared->violations.load(), 0);
}
