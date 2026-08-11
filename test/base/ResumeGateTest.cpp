// Copyright (c) 2026 Nevermore-. All rights reserved.

#include "Awaitable.hpp"
#include "CoroutineControl.hpp"
#include "Task.hpp"

#include <gtest/gtest.h>

#include <atomic>
#include <memory>
#include <optional>
#include <thread>
#include <vector>

namespace {

using slark::ResumeGate;
using slark::Task;

Task<void> immediateTask() {
    co_return;
}

struct GateFixture {
    Task<void> task;
    std::shared_ptr<ResumeGate> gate;

    GateFixture() {
        task = immediateTask();
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
    std::shared_ptr<ResumeGate> gate;
    {
        auto task = immediateTask();
        gate = slark::makeResumeGate(task.getHandle());
        gate->arm();
    }
    EXPECT_NO_THROW(gate->signal());
}

TEST(ResumeGateTest, ConcurrentSignalAndOwnerDestroyHaveSingleSafeOutcome) {
    constexpr int kIterations = 200;
    for (int i = 0; i < kIterations; ++i) {
        std::optional<Task<void>> task;
        task.emplace(immediateTask());
        auto gate = slark::makeResumeGate(task->getHandle());
        ASSERT_TRUE(gate->arm());

        std::atomic<bool> start{false};
        std::thread signaler([gate, &start] {
            while (!start.load(std::memory_order_acquire)) {
                std::this_thread::yield();
            }
            gate->signal();
        });
        std::thread owner([&task, &start] {
            while (!start.load(std::memory_order_acquire)) {
                std::this_thread::yield();
            }
            task.reset();
        });

        start.store(true, std::memory_order_release);
        signaler.join();
        owner.join();
        EXPECT_NO_THROW(gate->signal());
    }
}
