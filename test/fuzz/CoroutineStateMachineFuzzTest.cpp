#include "SharedCoroutineHandle.hpp"
#include "CoroutineControlBlock.hpp"
#include "Coroutine.hpp"
#include "CoroutineScheduler.hpp"

#include <gtest/gtest.h>
#include <random>
#include <thread>

using namespace slark;

TEST(SharedCoroutineHandleFuzzTest, RandomStateTransitions) {
    auto control = std::make_shared<CoroutineControlBlock>(std::coroutine_handle<>{});
    auto handle = std::make_shared<SharedCoroutineHandle>(control);

    std::atomic<int> notifyCount{0};
    std::atomic<int> suspendCount{0};
    std::atomic<int> resumeCount{0};

    auto worker = [&]() {
        std::mt19937 rng(static_cast<std::mt19937::result_type>(
            std::hash<std::thread::id>{}(std::this_thread::get_id())));
        for (int i = 0; i < 100; ++i) {
            int op = rng() % 3;
            switch (op) {
                case 0:
                    if (handle->notify()) notifyCount++;
                    break;
                case 1:
                    if (handle->suspend()) suspendCount++;
                    break;
                case 2:
                    if (handle->tryResume()) resumeCount++;
                    break;
            }
            std::this_thread::yield();
        }
    };

    std::thread t1(worker);
    std::thread t2(worker);
    t1.join();
    t2.join();

    EXPECT_LE(resumeCount.load(), 1);
}

TEST(SharedCoroutineHandleFuzzTest, ConcurrentNotifyAndResume) {
    auto control = std::make_shared<CoroutineControlBlock>(std::coroutine_handle<>{});
    auto handle = std::make_shared<SharedCoroutineHandle>(control);
    (void)handle->suspend();

    std::thread notifier([&]() { (void)handle->notify(); });
    std::thread resumer([&]() { handle->tryResume(); });
    notifier.join();
    resumer.join();
    SUCCEED();
}

TEST(CoroutineFuzzTest, ManyConcurrentCoroutines) {
    std::atomic<int> completed{0};
    constexpr int kCount = 50;

    std::vector<std::thread> threads;
    for (int i = 0; i < kCount; ++i) {
        threads.emplace_back([&]() {
            auto coro = [&]() -> Coroutine<void> { completed++; co_return; }();
            coro.resume();
        });
    }
    for (auto& t : threads) t.join();
    EXPECT_EQ(completed.load(), kCount);
}

TEST(CoroutineFuzzTest, CoroutinesWithTimersAndCancellation) {
    std::atomic<int> timerFired{0};
    auto& scheduler = CoroutineScheduler::instance();
    std::vector<TimerId> timers;

    for (int i = 0; i < 20; ++i) {
        auto id = scheduler.scheduleCallback(
            std::chrono::milliseconds(1 + (i % 5)),
            [&timerFired]() { timerFired++; });
        timers.push_back(id);
    }
    for (int i = 0; i < 10; ++i) {
        scheduler.cancel(timers[static_cast<size_t>(i)]);
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    scheduler.waitForAll();
    EXPECT_LE(timerFired.load(), 20);
}
