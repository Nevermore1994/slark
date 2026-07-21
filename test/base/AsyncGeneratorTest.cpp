//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//
#include "AsyncGenerator.hpp"

#include "Coroutine.hpp"
#include "CoroutineScheduler.hpp"
#include "ThreadPool.hpp"
#include "TimerManager.h"

#include <atomic>
#include <chrono>
#include <gtest/gtest.h>
#include <set>
#include <thread>
#include <vector>

using namespace slark;

namespace {

// 清理全局协程调度器和定时器状态，防止前一个测试的残留 job 干扰当前测试
void cleanupScheduler() {
    TimerManager::shareInstance().clear();
    CoroutineScheduler::instance().waitForAll();
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
}

// 每个 AsyncGenerator 测试的 RAII 清理守卫
struct AsyncGeneratorTestGuard {
    AsyncGeneratorTestGuard() { cleanupScheduler(); }
    ~AsyncGeneratorTestGuard() { cleanupScheduler(); }
};

} // namespace

namespace {

// 驱动 consumer 协程直到完成。pull-style generator 的 final_suspend 不自动 resume
// consumer，需要多次 resume 推动控制流。
// 注意：Coroutine::FinalAwaiter 使用 CoroutineScheduler 异步调度父协程恢复，
// 因此 resume() 后必须 drain scheduler，否则协程可能仍处于挂起状态。
template <typename T>
void resumeUntilDone(Coroutine<T>& coro) {
    // 先排空可能来自之前测试的残留 job，避免协程调度冲突
    cleanupScheduler();
    while (!coro.isDone()) {
        coro.resume();
        CoroutineScheduler::instance().waitForAll();
    }
    for (int32_t i = 0; i < 5; ++i) {
        CoroutineScheduler::instance().waitForAll();
    }
}

// 排空 generator：消费完剩余所有值，确保 generator 协程最终完成。
// 避免 generator 协程 frame 在挂起时被析构导致 SEGFAULT。
template <typename T>
void drainGenerator(AsyncGenerator<T>& gen) {
    while (!gen.isDone()) {
        auto coro = [&gen]() -> Coroutine<void> {
            auto value = co_await gen.next();
            (void)value;
        }();
        CoroutineScheduler::instance().waitForAll();
        while (!coro.isDone()) {
            coro.resume();
            CoroutineScheduler::instance().waitForAll();
        }
        for (int32_t i = 0; i < 5; ++i) {
            CoroutineScheduler::instance().waitForAll();
        }
    }
}

} // namespace

// 基础生成器测试
TEST(AsyncGeneratorTest, BasicGenerator) {
    auto gen = []() -> AsyncGenerator<int> {
        co_yield 1;
        co_yield 2;
        co_yield 3;
    }();

    auto coro = [&]() -> Coroutine<void> {
        std::vector<int> values;
        while (true) {
            auto value = co_await gen.next();
            if (!value.has_value()) {
                break;
            }
            values.push_back(*value);
        }
        EXPECT_EQ(values.size(), 3u);
        EXPECT_EQ(values[0], 1);
        EXPECT_EQ(values[1], 2);
        EXPECT_EQ(values[2], 3);
    }();

    resumeUntilDone(coro);
    EXPECT_TRUE(coro.isDone());
}

TEST(AsyncGeneratorTest, EmptyGenerator) {
    auto gen = []() -> AsyncGenerator<int> {
        co_return;
    }();

    auto coro = [&]() -> Coroutine<void> {
        auto value = co_await gen.next();
        EXPECT_FALSE(value.has_value());
    }();

    resumeUntilDone(coro);
    EXPECT_TRUE(coro.isDone());
}

TEST(AsyncGeneratorTest, SingleValueGenerator) {
    auto gen = []() -> AsyncGenerator<std::string> {
        co_yield "hello";
    }();

    auto coro = [&]() -> Coroutine<void> {
        auto value = co_await gen.next();
        EXPECT_TRUE(value.has_value());
        EXPECT_EQ(*value, "hello");

        auto value2 = co_await gen.next();
        EXPECT_FALSE(value2.has_value());
    }();

    resumeUntilDone(coro);
    EXPECT_TRUE(coro.isDone());
}

TEST(AsyncGeneratorTest, MoveOnlyType) {
    auto gen = []() -> AsyncGenerator<std::unique_ptr<int>> {
        co_yield std::make_unique<int>(42);
        co_yield std::make_unique<int>(100);
    }();

    auto coro = [&]() -> Coroutine<void> {
        auto value1 = co_await gen.next();
        EXPECT_TRUE(value1.has_value());
        EXPECT_EQ(**value1, 42);

        auto value2 = co_await gen.next();
        EXPECT_TRUE(value2.has_value());
        EXPECT_EQ(**value2, 100);

        auto value3 = co_await gen.next();
        EXPECT_FALSE(value3.has_value());
    }();

    resumeUntilDone(coro);
    EXPECT_TRUE(coro.isDone());
}

TEST(AsyncGeneratorTest, IsValid) {
    auto gen = []() -> AsyncGenerator<int> {
        co_yield 1;
    }();

    EXPECT_TRUE(gen.isValid());

    AsyncGenerator<int> moved = std::move(gen);
    EXPECT_FALSE(gen.isValid());
    EXPECT_TRUE(moved.isValid());
}

TEST(AsyncGeneratorTest, IsDone) {
    auto gen = []() -> AsyncGenerator<int> {
        co_yield 1;
        co_yield 2;
    }();

    EXPECT_FALSE(gen.isDone());

    auto coro = [&]() -> Coroutine<void> {
        co_await gen.next();
        co_await gen.next();
        auto value = co_await gen.next();
        EXPECT_FALSE(value.has_value());
    }();

    resumeUntilDone(coro);
    EXPECT_TRUE(gen.isDone());
}

// Transform 测试
TEST(AsyncGeneratorTest, Transform) {
    auto gen = []() -> AsyncGenerator<int> {
        co_yield 1;
        co_yield 2;
        co_yield 3;
    }();

    auto transformed = std::move(gen).transform([](int x) {
        return x * 2;
    });

    auto coro = [&]() -> Coroutine<void> {
        std::vector<int> values;
        while (true) {
            auto value = co_await transformed.next();
            if (!value.has_value()) {
                break;
            }
            values.push_back(*value);
        }
        EXPECT_EQ(values.size(), 3u);
        EXPECT_EQ(values[0], 2);
        EXPECT_EQ(values[1], 4);
        EXPECT_EQ(values[2], 6);
    }();

    resumeUntilDone(coro);
    EXPECT_TRUE(coro.isDone());
}

TEST(AsyncGeneratorTest, TransformStringType) {
    auto gen = []() -> AsyncGenerator<int> {
        co_yield 1;
        co_yield 2;
    }();

    auto transformed = std::move(gen).transform([](int x) {
        return std::to_string(x);
    });

    auto coro = [&]() -> Coroutine<void> {
        std::vector<std::string> values;
        while (true) {
            auto value = co_await transformed.next();
            if (!value.has_value()) {
                break;
            }
            values.push_back(*value);
        }
        EXPECT_EQ(values.size(), 2u);
        EXPECT_EQ(values[0], "1");
        EXPECT_EQ(values[1], "2");
    }();

    resumeUntilDone(coro);
    EXPECT_TRUE(coro.isDone());
}

// Filter 测试
TEST(AsyncGeneratorTest, Filter) {
    auto gen = []() -> AsyncGenerator<int> {
        co_yield 1;
        co_yield 2;
        co_yield 3;
        co_yield 4;
        co_yield 5;
    }();

    auto filtered = std::move(gen).filter([](int x) {
        return x % 2 == 0;
    });

    auto coro = [&]() -> Coroutine<void> {
        std::vector<int> values;
        while (true) {
            auto value = co_await filtered.next();
            if (!value.has_value()) {
                break;
            }
            values.push_back(*value);
        }
        EXPECT_EQ(values.size(), 2u);
        EXPECT_EQ(values[0], 2);
        EXPECT_EQ(values[1], 4);
    }();

    resumeUntilDone(coro);
    EXPECT_TRUE(coro.isDone());
}

TEST(AsyncGeneratorTest, FilterAll) {
    auto gen = []() -> AsyncGenerator<int> {
        co_yield 1;
        co_yield 3;
        co_yield 5;
    }();

    auto filtered = std::move(gen).filter([](int x) {
        return x % 2 == 0;
    });

    auto coro = [&]() -> Coroutine<void> {
        std::vector<int> values;
        while (true) {
            auto value = co_await filtered.next();
            if (!value.has_value()) {
                break;
            }
            values.push_back(*value);
        }
        EXPECT_EQ(values.size(), 0u);
    }();

    resumeUntilDone(coro);
    EXPECT_TRUE(coro.isDone());
}

// Take 测试
TEST(AsyncGeneratorTest, Take) {
    auto gen = []() -> AsyncGenerator<int> {
        for (int i = 0; i < 100; ++i) {
            co_yield i;
        }
    }();

    auto taken = std::move(gen).take(5);

    auto coro = [&]() -> Coroutine<void> {
        std::vector<int> values;
        while (true) {
            auto value = co_await taken.next();
            if (!value.has_value()) {
                break;
            }
            values.push_back(*value);
        }
        EXPECT_EQ(values.size(), 5u);
        for (size_t i = 0; i < 5; ++i) {
            EXPECT_EQ(values[i], i);
        }
    }();

    resumeUntilDone(coro);
    EXPECT_TRUE(coro.isDone());
}

TEST(AsyncGeneratorTest, TakeZero) {
    auto gen = []() -> AsyncGenerator<int> {
        co_yield 1;
        co_yield 2;
    }();

    auto taken = std::move(gen).take(0);

    auto coro = [&]() -> Coroutine<void> {
        auto value = co_await taken.next();
        EXPECT_FALSE(value.has_value());
    }();

    resumeUntilDone(coro);
    EXPECT_TRUE(coro.isDone());
}

TEST(AsyncGeneratorTest, TakeMoreThanAvailable) {
    auto gen = []() -> AsyncGenerator<int> {
        co_yield 1;
        co_yield 2;
    }();

    auto taken = std::move(gen).take(10);

    auto coro = [&]() -> Coroutine<void> {
        int count = 0;
        while (true) {
            auto value = co_await taken.next();
            if (!value.has_value()) {
                break;
            }
            count++;
        }
        EXPECT_EQ(count, 2);
    }();

    resumeUntilDone(coro);
    EXPECT_TRUE(coro.isDone());
}

// Drop 测试
TEST(AsyncGeneratorTest, Drop) {
    auto gen = []() -> AsyncGenerator<int> {
        for (int i = 0; i < 5; ++i) {
            co_yield i;
        }
    }();

    auto dropped = std::move(gen).drop(2);

    auto coro = [&]() -> Coroutine<void> {
        std::vector<int> values;
        while (true) {
            auto value = co_await dropped.next();
            if (!value.has_value()) {
                break;
            }
            values.push_back(*value);
        }
        EXPECT_EQ(values.size(), 3u);
        EXPECT_EQ(values[0], 2);
        EXPECT_EQ(values[1], 3);
        EXPECT_EQ(values[2], 4);
    }();

    resumeUntilDone(coro);
    EXPECT_TRUE(coro.isDone());
}

TEST(AsyncGeneratorTest, DropAll) {
    auto gen = []() -> AsyncGenerator<int> {
        co_yield 1;
        co_yield 2;
    }();

    auto dropped = std::move(gen).drop(10);

    auto coro = [&]() -> Coroutine<void> {
        auto value = co_await dropped.next();
        EXPECT_FALSE(value.has_value());
    }();

    resumeUntilDone(coro);
    EXPECT_TRUE(coro.isDone());
}

// TakeWhile 测试
TEST(AsyncGeneratorTest, TakeWhile) {
    auto gen = []() -> AsyncGenerator<int> {
        for (int i = 0; i < 10; ++i) {
            co_yield i;
        }
    }();

    auto taken = std::move(gen).takeWhile([](int x) {
        return x < 5;
    });

    auto coro = [&]() -> Coroutine<void> {
        std::vector<int> values;
        while (true) {
            auto value = co_await taken.next();
            if (!value.has_value()) {
                break;
            }
            values.push_back(*value);
        }
        EXPECT_EQ(values.size(), 5u);
        for (size_t i = 0; i < 5; ++i) {
            EXPECT_EQ(values[i], i);
        }
    }();

    resumeUntilDone(coro);
    EXPECT_TRUE(coro.isDone());
}

// DropWhile 测试
TEST(AsyncGeneratorTest, DropWhile) {
    auto gen = []() -> AsyncGenerator<int> {
        for (int i = 0; i < 10; ++i) {
            co_yield i;
        }
    }();

    auto dropped = std::move(gen).dropWhile([](int x) {
        return x < 5;
    });

    auto coro = [&]() -> Coroutine<void> {
        std::vector<int> values;
        while (true) {
            auto value = co_await dropped.next();
            if (!value.has_value()) {
                break;
            }
            values.push_back(*value);
        }
        EXPECT_EQ(values.size(), 5u);
        for (size_t i = 0; i < 5; ++i) {
            EXPECT_EQ(values[i], i + 5);
        }
    }();

    resumeUntilDone(coro);
    EXPECT_TRUE(coro.isDone());
}

// Enumerate 测试
TEST(AsyncGeneratorTest, Enumerate) {
    auto gen = []() -> AsyncGenerator<std::string> {
        co_yield "a";
        co_yield "b";
        co_yield "c";
    }();

    auto enumerated = std::move(gen).enumerate();

    auto coro = [&]() -> Coroutine<void> {
        std::vector<std::pair<int64_t, std::string>> values;
        while (true) {
            auto value = co_await enumerated.next();
            if (!value.has_value()) {
                break;
            }
            values.push_back(*value);
        }
        EXPECT_EQ(values.size(), 3u);
        EXPECT_EQ(values[0].first, 0);
        EXPECT_EQ(values[0].second, "a");
        EXPECT_EQ(values[1].first, 1);
        EXPECT_EQ(values[1].second, "b");
        EXPECT_EQ(values[2].first, 2);
        EXPECT_EQ(values[2].second, "c");
    }();

    resumeUntilDone(coro);
    EXPECT_TRUE(coro.isDone());
}

// Fold 测试
TEST(AsyncGeneratorTest, Fold) {
    auto gen = []() -> AsyncGenerator<int> {
        co_yield 1;
        co_yield 2;
        co_yield 3;
        co_yield 4;
    }();

    auto coro = [&]() -> Coroutine<int> {
        co_return co_await std::move(gen).fold(0, [](int acc, int x) {
            return acc + x;
        });
    }();

    resumeUntilDone(coro);
    EXPECT_TRUE(coro.isDone());
    EXPECT_EQ(coro.getResult(), 10);
}

TEST(AsyncGeneratorTest, FoldStringConcat) {
    auto gen = []() -> AsyncGenerator<std::string> {
        co_yield "hello";
        co_yield " ";
        co_yield "world";
    }();

    auto coro = [&]() -> Coroutine<std::string> {
        co_return co_await std::move(gen).fold(std::string(""),
                                               [](std::string acc, const std::string& x) {
                                                   return acc + x;
                                               });
    }();

    resumeUntilDone(coro);
    EXPECT_TRUE(coro.isDone());
    EXPECT_EQ(coro.getResult(), "hello world");
}

// Count 测试
TEST(AsyncGeneratorTest, Count) {
    auto gen = []() -> AsyncGenerator<int> {
        for (int i = 0; i < 10; ++i) {
            co_yield i;
        }
    }();

    auto coro = [&]() -> Coroutine<int64_t> {
        co_return co_await std::move(gen).count();
    }();

    resumeUntilDone(coro);
    EXPECT_TRUE(coro.isDone());
    EXPECT_EQ(coro.getResult(), 10);
}

TEST(AsyncGeneratorTest, CountEmpty) {
    auto gen = []() -> AsyncGenerator<int> {
        co_return;
    }();

    auto coro = [&]() -> Coroutine<int64_t> {
        co_return co_await std::move(gen).count();
    }();

    resumeUntilDone(coro);
    EXPECT_TRUE(coro.isDone());
    EXPECT_EQ(coro.getResult(), 0);
}

// Find 测试
TEST(AsyncGeneratorTest, Find) {
    auto gen = []() -> AsyncGenerator<int> {
        co_yield 1;
        co_yield 2;
        co_yield 3;
        co_yield 4;
    }();

    auto coro = [&]() -> Coroutine<std::optional<int>> {
        co_return co_await std::move(gen).find([](int x) {
            return x > 2;
        });
    }();

    resumeUntilDone(coro);
    EXPECT_TRUE(coro.isDone());
    auto result = coro.getResult();
    EXPECT_TRUE(result.has_value());
    EXPECT_EQ(*result, 3);
}

TEST(AsyncGeneratorTest, FindNotFound) {
    auto gen = []() -> AsyncGenerator<int> {
        co_yield 1;
        co_yield 2;
    }();

    auto coro = [&]() -> Coroutine<std::optional<int>> {
        co_return co_await std::move(gen).find([](int x) {
            return x > 10;
        });
    }();

    resumeUntilDone(coro);
    EXPECT_TRUE(coro.isDone());
    auto result = coro.getResult();
    EXPECT_FALSE(result.has_value());
}

// Any 测试
TEST(AsyncGeneratorTest, AnyTrue) {
    auto gen = []() -> AsyncGenerator<int> {
        co_yield 1;
        co_yield 2;
        co_yield 3;
    }();

    auto coro = [&]() -> Coroutine<bool> {
        co_return co_await std::move(gen).any([](int x) {
            return x == 2;
        });
    }();

    resumeUntilDone(coro);
    EXPECT_TRUE(coro.isDone());
    EXPECT_TRUE(coro.getResult());
}

TEST(AsyncGeneratorTest, AnyFalse) {
    auto gen = []() -> AsyncGenerator<int> {
        co_yield 1;
        co_yield 2;
        co_yield 3;
    }();

    auto coro = [&]() -> Coroutine<bool> {
        co_return co_await std::move(gen).any([](int x) {
            return x > 10;
        });
    }();

    resumeUntilDone(coro);
    EXPECT_TRUE(coro.isDone());
    EXPECT_FALSE(coro.getResult());
}

// All 测试
TEST(AsyncGeneratorTest, AllTrue) {
    auto gen = []() -> AsyncGenerator<int> {
        co_yield 2;
        co_yield 4;
        co_yield 6;
    }();

    auto coro = [&]() -> Coroutine<bool> {
        co_return co_await std::move(gen).all([](int x) {
            return x % 2 == 0;
        });
    }();

    resumeUntilDone(coro);
    EXPECT_TRUE(coro.isDone());
    EXPECT_TRUE(coro.getResult());
}

TEST(AsyncGeneratorTest, AllFalse) {
    auto gen = []() -> AsyncGenerator<int> {
        co_yield 2;
        co_yield 3;
        co_yield 4;
    }();

    auto coro = [&]() -> Coroutine<bool> {
        co_return co_await std::move(gen).all([](int x) {
            return x % 2 == 0;
        });
    }();

    resumeUntilDone(coro);
    EXPECT_TRUE(coro.isDone());
    EXPECT_FALSE(coro.getResult());
}

// ForEach 测试
TEST(AsyncGeneratorTest, ForEach) {
    auto gen = []() -> AsyncGenerator<int> {
        co_yield 1;
        co_yield 2;
        co_yield 3;
    }();

    std::vector<int> collected;

    auto coro = [&]() -> Coroutine<void> {
        co_await std::move(gen).forEach([&collected](int x) {
            collected.push_back(x);
        });
    }();

    resumeUntilDone(coro);
    EXPECT_TRUE(coro.isDone());
    EXPECT_EQ(collected.size(), 3u);
    EXPECT_EQ(collected[0], 1);
    EXPECT_EQ(collected[1], 2);
    EXPECT_EQ(collected[2], 3);
}

// Collect 测试
TEST(AsyncGeneratorTest, CollectVector) {
    auto gen = []() -> AsyncGenerator<int> {
        co_yield 1;
        co_yield 2;
        co_yield 3;
    }();

    auto coro = [&]() -> Coroutine<std::vector<int>> {
        co_return co_await std::move(gen).collect<std::vector>();
    }();

    resumeUntilDone(coro);
    EXPECT_TRUE(coro.isDone());
    auto result = coro.getResult();
    EXPECT_EQ(result.size(), 3u);
    EXPECT_EQ(result[0], 1);
    EXPECT_EQ(result[1], 2);
    EXPECT_EQ(result[2], 3);
}

// 链式操作测试
TEST(AsyncGeneratorTest, ChainedOperations) {
    auto gen = []() -> AsyncGenerator<int> {
        for (int i = 0; i < 20; ++i) {
            co_yield i;
        }
    }();

    auto coro = [&]() -> Coroutine<int64_t> {
        auto filtered = std::move(gen).filter([](int x) {
            return x % 2 == 0;
        });
        auto transformed = std::move(filtered).transform([](int x) {
            return x * 10;
        });
        auto taken = std::move(transformed).take(3);
        co_return co_await std::move(taken).count();
    }();

    resumeUntilDone(coro);
    EXPECT_TRUE(coro.isDone());
    EXPECT_EQ(coro.getResult(), 3);
}

TEST(AsyncGeneratorTest, ComplexPipeline) {
    auto gen = []() -> AsyncGenerator<int> {
        for (int i = 1; i <= 10; ++i) {
            co_yield i;
        }
    }();

    auto coro = [&]() -> Coroutine<int> {
        auto dropped = std::move(gen).drop(2);
        auto taken = std::move(dropped).take(5);
        auto transformed = std::move(taken).transform([](int x) {
            return x * x;
        });
        co_return co_await std::move(transformed).fold(0, [](int acc, int x) {
            return acc + x;
        });
    }();

    resumeUntilDone(coro);
    EXPECT_TRUE(coro.isDone());
    // drop(2) 跳过 1,2；take(5) 取 3,4,5,6,7；平方和 = 9+16+25+36+49 = 135
    EXPECT_EQ(coro.getResult(), 135);
}

// 异常处理测试
TEST(AsyncGeneratorTest, ExceptionInGenerator) {
    auto gen = []() -> AsyncGenerator<int> {
        co_yield 1;
        throw std::runtime_error("test exception");
        co_yield 2;
    }();

    auto coro = [&]() -> Coroutine<void> {
        auto value1 = co_await gen.next();
        EXPECT_TRUE(value1.has_value());
        EXPECT_EQ(*value1, 1);

        EXPECT_THROW({ [[maybe_unused]] auto value2 = co_await gen.next(); }, std::runtime_error);
    }();

    resumeUntilDone(coro);
    EXPECT_TRUE(coro.isDone());
}

// 多线程并发测试
TEST(AsyncGeneratorTest, ConcurrentAccess) {
    ThreadPool pool(ThreadPoolConfig{4});
    std::atomic<int> counter{0};

    for (int i = 0; i < 10; ++i) {
        pool.postJob([&counter, i]() {
            auto gen = [i]() -> AsyncGenerator<int> {
                co_yield i;
                co_yield i * 2;
            }();

            auto coro = [&]() -> Coroutine<void> {
                int sum = 0;
                while (true) {
                    auto value = co_await gen.next();
                    if (!value.has_value()) {
                        break;
                    }
                    sum += *value;
                }
                counter += sum;
            }();

            resumeUntilDone(coro);
        });
    }

    pool.waitForAll();
    int expectedSum = 0;
    for (int i = 0; i < 10; ++i) {
        expectedSum += i + i * 2;
    }
    EXPECT_EQ(counter.load(), expectedSum);
}

// ---- 补充边界测试 ----

// TakeWhile 空序列
TEST(AsyncGeneratorTest, TakeWhileEmpty) {
    auto gen = []() -> AsyncGenerator<int> {
        co_return;
    }();

    auto taken = std::move(gen).takeWhile([](int x) {
        return x < 5;
    });

    auto coro = [&]() -> Coroutine<void> {
        auto value = co_await taken.next();
        EXPECT_FALSE(value.has_value());
    }();

    resumeUntilDone(coro);
    EXPECT_TRUE(coro.isDone());
}

// TakeWhile 全部满足条件
TEST(AsyncGeneratorTest, TakeWhileAll) {
    auto gen = []() -> AsyncGenerator<int> {
        co_yield 1;
        co_yield 2;
        co_yield 3;
    }();

    auto taken = std::move(gen).takeWhile([](int x) {
        return x < 100;
    });

    auto coro = [&]() -> Coroutine<int64_t> {
        co_return co_await std::move(taken).count();
    }();

    resumeUntilDone(coro);
    EXPECT_EQ(coro.getResult(), 3);
}

// TakeWhile 第一个就不满足
TEST(AsyncGeneratorTest, TakeWhileNone) {
    auto gen = []() -> AsyncGenerator<int> {
        co_yield 10;
        co_yield 20;
    }();

    auto taken = std::move(gen).takeWhile([](int x) {
        return x < 5;
    });

    auto coro = [&]() -> Coroutine<void> {
        auto value = co_await taken.next();
        EXPECT_FALSE(value.has_value());
    }();

    resumeUntilDone(coro);
    EXPECT_TRUE(coro.isDone());
}

// DropWhile 全部丢弃
TEST(AsyncGeneratorTest, DropWhileAll) {
    auto gen = []() -> AsyncGenerator<int> {
        co_yield 1;
        co_yield 2;
        co_yield 3;
    }();

    auto dropped = std::move(gen).dropWhile([](int x) {
        return x < 100;
    });

    auto coro = [&]() -> Coroutine<void> {
        auto value = co_await dropped.next();
        EXPECT_FALSE(value.has_value());
    }();

    resumeUntilDone(coro);
    EXPECT_TRUE(coro.isDone());
}

// DropWhile 第一个就不满足（全部保留）
TEST(AsyncGeneratorTest, DropWhileNone) {
    auto gen = []() -> AsyncGenerator<int> {
        co_yield 10;
        co_yield 20;
        co_yield 30;
    }();

    auto dropped = std::move(gen).dropWhile([](int x) {
        return x < 5;
    });

    auto coro = [&]() -> Coroutine<int64_t> {
        co_return co_await std::move(dropped).count();
    }();

    resumeUntilDone(coro);
    EXPECT_EQ(coro.getResult(), 3);
}

// Any 空序列返回 false
TEST(AsyncGeneratorTest, AnyEmpty) {
    auto gen = []() -> AsyncGenerator<int> {
        co_return;
    }();

    auto coro = [&]() -> Coroutine<bool> {
        co_return co_await std::move(gen).any([](int x) {
            return x > 0;
        });
    }();

    resumeUntilDone(coro);
    EXPECT_FALSE(coro.getResult());
}

// All 空序列返回 true（vacuous truth）
TEST(AsyncGeneratorTest, AllEmpty) {
    auto gen = []() -> AsyncGenerator<int> {
        co_return;
    }();

    auto coro = [&]() -> Coroutine<bool> {
        co_return co_await std::move(gen).all([](int x) {
            return x > 0;
        });
    }();

    resumeUntilDone(coro);
    EXPECT_TRUE(coro.getResult());
}

// Find 命中第一个元素
TEST(AsyncGeneratorTest, FindFirst) {
    auto gen = []() -> AsyncGenerator<int> {
        co_yield 5;
        co_yield 10;
        co_yield 15;
    }();

    auto coro = [&]() -> Coroutine<std::optional<int>> {
        co_return co_await std::move(gen).find([](int x) {
            return x >= 5;
        });
    }();

    resumeUntilDone(coro);
    EXPECT_TRUE(coro.getResult().has_value());
    EXPECT_EQ(*coro.getResult(), 5);
}

// Enumerate 空序列
TEST(AsyncGeneratorTest, EnumerateEmpty) {
    auto gen = []() -> AsyncGenerator<int> {
        co_return;
    }();

    auto enumerated = std::move(gen).enumerate();

    auto coro = [&]() -> Coroutine<void> {
        auto value = co_await enumerated.next();
        EXPECT_FALSE(value.has_value());
    }();

    resumeUntilDone(coro);
    EXPECT_TRUE(coro.isDone());
}

// Fold 空序列返回初始值
TEST(AsyncGeneratorTest, FoldEmpty) {
    auto gen = []() -> AsyncGenerator<int> {
        co_return;
    }();

    auto coro = [&]() -> Coroutine<int> {
        co_return co_await std::move(gen).fold(42, [](int acc, int x) {
            return acc + x;
        });
    }();

    resumeUntilDone(coro);
    EXPECT_EQ(coro.getResult(), 42);
}

// Collect<std::set> 去重
TEST(AsyncGeneratorTest, CollectSet) {
    auto gen = []() -> AsyncGenerator<int> {
        co_yield 3;
        co_yield 1;
        co_yield 2;
        co_yield 1; // 重复
        co_yield 3; // 重复
    }();

    auto coro = [&]() -> Coroutine<std::set<int>> {
        co_return co_await std::move(gen).collect<std::set>();
    }();

    resumeUntilDone(coro);
    EXPECT_TRUE(coro.isDone());
    auto result = coro.getResult();
    EXPECT_EQ(result.size(), 3u);
    EXPECT_TRUE(result.contains(1));
    EXPECT_TRUE(result.contains(2));
    EXPECT_TRUE(result.contains(3));
}

// ---- Pipe Operator 测试 ----

// Pipe operator 测试
TEST(AsyncGeneratorPipeTest, TransformPipe) {
    AsyncGeneratorTestGuard guard;
    auto gen = []() -> AsyncGenerator<int> {
        co_yield 1;
        co_yield 2;
        co_yield 3;
    }();

    auto transformed = std::move(gen) | transform([](int x) {
                           return x * 10;
                       });

    auto coro = [&]() -> Coroutine<std::vector<int>> {
        co_return co_await std::move(transformed).collect<std::vector>();
    }();

    resumeUntilDone(coro);
    auto result = coro.getResult();
    ASSERT_EQ(result.size(), 3u);
    EXPECT_EQ(result[0], 10);
    EXPECT_EQ(result[1], 20);
    EXPECT_EQ(result[2], 30);
}

TEST(AsyncGeneratorPipeTest, FilterPipe) {
    AsyncGeneratorTestGuard guard;
    auto gen = []() -> AsyncGenerator<int> {
        for (int i = 1; i <= 6; ++i) {
            co_yield i;
        }
    }();

    auto filtered = std::move(gen) | filter([](int x) {
                        return x % 2 == 0;
                    });

    auto coro = [&]() -> Coroutine<std::vector<int>> {
        co_return co_await std::move(filtered).collect<std::vector>();
    }();

    resumeUntilDone(coro);
    auto result = coro.getResult();
    ASSERT_EQ(result.size(), 3u);
    EXPECT_EQ(result[0], 2);
    EXPECT_EQ(result[1], 4);
    EXPECT_EQ(result[2], 6);
}

TEST(AsyncGeneratorPipeTest, TakePipe) {
    AsyncGeneratorTestGuard guard;
    auto gen = []() -> AsyncGenerator<int> {
        for (int i = 0; i < 100; ++i) {
            co_yield i;
        }
    }();

    auto taken = std::move(gen) | take(3);

    auto coro = [&]() -> Coroutine<int64_t> {
        co_return co_await std::move(taken).count();
    }();

    resumeUntilDone(coro);
    EXPECT_EQ(coro.getResult(), 3);
}

TEST(AsyncGeneratorPipeTest, DropPipe) {
    AsyncGeneratorTestGuard guard;
    auto gen = []() -> AsyncGenerator<int> {
        for (int i = 0; i < 5; ++i) {
            co_yield i;
        }
    }();

    auto dropped = std::move(gen) | drop(3);

    auto coro = [&]() -> Coroutine<std::vector<int>> {
        co_return co_await std::move(dropped).collect<std::vector>();
    }();

    resumeUntilDone(coro);
    auto result = coro.getResult();
    ASSERT_EQ(result.size(), 2u);
    EXPECT_EQ(result[0], 3);
    EXPECT_EQ(result[1], 4);
}

TEST(AsyncGeneratorPipeTest, TakeWhilePipe) {
    AsyncGeneratorTestGuard guard;
    auto gen = []() -> AsyncGenerator<int> {
        for (int i = 0; i < 10; ++i) {
            co_yield i;
        }
    }();

    auto taken = std::move(gen) | takeWhile([](int x) {
                     return x < 4;
                 });

    auto coro = [&]() -> Coroutine<int64_t> {
        co_return co_await std::move(taken).count();
    }();

    resumeUntilDone(coro);
    EXPECT_EQ(coro.getResult(), 4);
}

TEST(AsyncGeneratorPipeTest, DropWhilePipe) {
    AsyncGeneratorTestGuard guard;
    auto gen = []() -> AsyncGenerator<int> {
        for (int i = 0; i < 6; ++i) {
            co_yield i;
        }
    }();

    auto dropped = std::move(gen) | dropWhile([](int x) {
                       return x < 3;
                   });

    auto coro = [&]() -> Coroutine<std::vector<int>> {
        co_return co_await std::move(dropped).collect<std::vector>();
    }();

    resumeUntilDone(coro);
    auto result = coro.getResult();
    ASSERT_EQ(result.size(), 3u);
    EXPECT_EQ(result[0], 3);
}

TEST(AsyncGeneratorPipeTest, EnumeratePipe) {
    AsyncGeneratorTestGuard guard;
    auto gen = []() -> AsyncGenerator<std::string> {
        co_yield "x";
        co_yield "y";
    }();

    auto enumerated = std::move(gen) | enumerate();

    auto coro = [&]() -> Coroutine<void> {
        auto v0 = co_await enumerated.next();
        EXPECT_TRUE(v0.has_value());
        EXPECT_EQ(v0->first, 0);
        EXPECT_EQ(v0->second, "x");

        auto v1 = co_await enumerated.next();
        EXPECT_TRUE(v1.has_value());
        EXPECT_EQ(v1->first, 1);
        EXPECT_EQ(v1->second, "y");

        auto v2 = co_await enumerated.next();
        EXPECT_FALSE(v2.has_value());
    }();

    resumeUntilDone(coro);
    EXPECT_TRUE(coro.isDone());
}

// 链式 pipe 操作
TEST(AsyncGeneratorPipeTest, ChainedPipe) {
    AsyncGeneratorTestGuard guard;
    auto gen = []() -> AsyncGenerator<int> {
        for (int i = 1; i <= 10; ++i) {
            co_yield i;
        }
    }();

    // filter 偶数 -> transform *3 -> take 3 -> collect
    auto pipeline = std::move(gen) | filter([](int x) {
                        return x % 2 == 0;
                    }) |
                    transform([](int x) {
                        return x * 3;
                    }) |
                    take(3);
    auto result_coro = [&pipeline]() -> Coroutine<std::vector<int>> {
        co_return co_await std::move(pipeline).collect<std::vector>();
    }();

    resumeUntilDone(result_coro);
    auto result = result_coro.getResult();
    ASSERT_EQ(result.size(), 3u);
    EXPECT_EQ(result[0], 6);  // 2*3
    EXPECT_EQ(result[1], 12); // 4*3
    EXPECT_EQ(result[2], 18); // 6*3
}

// ---- 大数据量压力测试 ----

TEST(AsyncGeneratorTest, LargeSequence) {
    constexpr int64_t kCount = 10000;
    auto gen = []() -> AsyncGenerator<int64_t> {
        for (int64_t i = 0; i < kCount; ++i) {
            co_yield i;
        }
    }();

    auto coro = [&]() -> Coroutine<int64_t> {
        co_return co_await std::move(gen).count();
    }();

    resumeUntilDone(coro);
    EXPECT_EQ(coro.getResult(), kCount);
}

TEST(AsyncGeneratorTest, LargeSequenceFold) {
    constexpr int64_t kN = 1000;
    auto gen = []() -> AsyncGenerator<int64_t> {
        for (int64_t i = 1; i <= kN; ++i) {
            co_yield i;
        }
    }();

    auto coro = [&]() -> Coroutine<int64_t> {
        co_return co_await std::move(gen).fold(int64_t{0}, [](int64_t acc, int64_t x) {
            return acc + x;
        });
    }();

    resumeUntilDone(coro);
    // 1+2+...+1000 = 500500
    EXPECT_EQ(coro.getResult(), kN * (kN + 1) / 2);
}

// final_suspend 回归测试：generator 提前终止后 consumer 通过 next() 探测完成
TEST(AsyncGeneratorTest, FinalSuspendConsumerProbesCompletion) {
    std::atomic<bool> generatorTerminated{false};

    // 模拟 processStreaming 被 cancel 提前 co_return 的场景
    auto gen = [&generatorTerminated]() -> AsyncGenerator<int> {
        co_yield 1;
        co_yield 2;
        // 模拟 cancel 触发提前终止
        generatorTerminated.store(true);
        co_return;
    }();

    auto consumer = [&]() -> Coroutine<void> {
        auto v1 = co_await gen.next();
        EXPECT_TRUE(v1.has_value());
        EXPECT_EQ(*v1, 1);

        auto v2 = co_await gen.next();
        EXPECT_TRUE(v2.has_value());
        EXPECT_EQ(*v2, 2);

        // generator 已 co_return，consumer 通过 next() 探测到 nullopt
        auto v3 = co_await gen.next();
        EXPECT_FALSE(v3.has_value());
    }();

    consumer.resume();
    EXPECT_TRUE(consumer.isDone());
    EXPECT_TRUE(generatorTerminated.load());
}

// final_suspend 回归测试：嵌套 generator（streamingExecute → processStreaming）提前终止
TEST(AsyncGeneratorTest, NestedGeneratorFinalSuspend) {
    // 内层 generator 提前 co_return（模拟 processStreaming cancel）
    auto inner = []() -> AsyncGenerator<int> {
        co_yield 10;
        co_yield 20;
        co_return; // 提前终止
    }();

    // 外层 generator 转发内层（模拟 streamingExecute）
    auto outer = [](AsyncGenerator<int> gen) -> AsyncGenerator<int> {
        while (true) {
            auto v = co_await gen.next();
            if (!v.has_value()) {
                break;
            }
            co_yield *v;
        }
    }(std::move(inner));

    // consumer 正常消费
    auto consumer = [&]() -> Coroutine<void> {
        auto v1 = co_await outer.next();
        EXPECT_TRUE(v1.has_value());
        EXPECT_EQ(*v1, 10);

        auto v2 = co_await outer.next();
        EXPECT_TRUE(v2.has_value());
        EXPECT_EQ(*v2, 20);

        auto v3 = co_await outer.next();
        EXPECT_FALSE(v3.has_value());
    }();

    consumer.resume();
    EXPECT_TRUE(consumer.isDone());
}
