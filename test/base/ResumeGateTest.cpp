// Copyright (c) 2026 Nevermore-. All rights reserved.

#include "Awaitable.hpp"
#include "CoroutineControl.hpp"
#include "Task.hpp"

#include <gtest/gtest.h>

#include <atomic>
#include <memory>
#include <thread>
#include <vector>

namespace {

using slark::CompletionAwaiter;
using slark::ResumeGate;
using slark::Task;

Task<void> suspendedOn(std::shared_ptr<CompletionAwaiter<void>> awaiter) {
    co_await *awaiter;
}

struct GateFixture {
    std::shared_ptr<CompletionAwaiter<void>> awaiter =
        std::make_shared<CompletionAwaiter<void>>();
    Task<void> task;
    std::shared_ptr<ResumeGate> gate;

    GateFixture() {
        task = suspendedOn(awaiter);
        task.resume();
        gate = slark::makeResumeGate(task.getHandle());
    }

    bool done() const noexcept {
        return task.isDone();
    }
};

}  // namespace

TEST(ResumeGateTest, SignalBeforeArmIsCoalesced) {
    GateFixture fx;
    fx.gate->signal();          // Pending -> Signaled
    EXPECT_FALSE(fx.gate->arm());
    EXPECT_FALSE(fx.done());
}

TEST(ResumeGateTest, ArmThenSignalResumesOnce) {
    GateFixture fx;
    EXPECT_TRUE(fx.gate->arm());
    fx.gate->signal();
    EXPECT_TRUE(fx.done());
}

TEST(ResumeGateTest, ConcurrentSignalHasSingleWinner) {
    GateFixture fx;
    ASSERT_TRUE(fx.gate->arm());
    std::vector<std::thread> workers;
    for (int i = 0; i < 16; ++i) {
        workers.emplace_back([&fx] { fx.gate->signal(); });
    }
    for (auto& worker : workers) {
        worker.join();
    }
    EXPECT_TRUE(fx.done());
}

TEST(ResumeGateTest, SignalAfterDisarmIsNoop) {
    GateFixture fx;
    ASSERT_TRUE(fx.gate->arm());
    fx.gate->disarm();
    fx.gate->signal();
    EXPECT_FALSE(fx.done());
}

TEST(ResumeGateTest, SignalBeforeArmAndDisarm) {
    GateFixture fx;
    fx.gate->signal();          // Pending -> Signaled
    fx.gate->disarm();
    EXPECT_FALSE(fx.gate->arm());
    EXPECT_FALSE(fx.done());
}

TEST(ResumeGateTest, SignalAfterFrameDestroyIsSafe) {
    auto awaiter = std::make_shared<CompletionAwaiter<void>>();
    std::shared_ptr<ResumeGate> gate;
    {
        auto task = suspendedOn(awaiter);
        task.resume();
        gate = slark::makeResumeGate(task.getHandle());
        gate->arm();
    }
    EXPECT_NO_THROW(gate->signal());
}
