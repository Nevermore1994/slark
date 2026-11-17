// Copyright (c) 2026 Nevermore-. All rights reserved.

#include <gtest/gtest.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>

#include "AsyncGenerator.hpp"
#include "Awaitable.hpp"
#include "CancellationToken.h"
#include "CoScope.hpp"
#include "CoWhenAll.hpp"
#include "CoroutineTestSupport.h"
#include "Error.h"
#include "Job.hpp"
#include "Task.hpp"

using namespace std::chrono_literals;

using slark::AsyncGenerator;
using slark::CancellationToken;
using slark::CancellationTokenTestAccess;
using slark::CancelledException;
using slark::CompletionAwaiter;
using slark::Task;
using slark::TimerAwaiter;
using slark::coro::AsyncScope;

namespace {

struct Probe {
    std::atomic<bool> cancelled{false};
    std::atomic<bool> completed{false};
};

Task<void> parkedLeaf(std::shared_ptr<Probe> probe) {
    try {
        co_await TimerAwaiter(5000ms);
        probe->completed = true;
    } catch (const CancelledException&) {
        probe->cancelled = true;
        throw;
    }
}

Task<void> quickLeaf() {
    co_return;
}

Task<void> failAfter(std::shared_ptr<CompletionAwaiter<void>> gate) {
    co_await *gate;
    throw std::runtime_error("sibling failed");
}

std::size_t callbacksOn(const CancellationToken& token) {
    return CancellationTokenTestAccess::callbackCount(token);
}

constexpr std::size_t kNotDone = std::numeric_limits<std::size_t>::max();

Task<void> awaitLeaf(std::shared_ptr<Probe> probe) {
    co_await parkedLeaf(std::move(probe));
}

Task<void> awaitOwnTokenChildren(int rounds, const CancellationToken* upstream,
                                 std::size_t* maxSeen) {
    for (int i = 0; i < rounds; ++i) {
        auto child = quickLeaf();
        child.setCancellationToken(CancellationToken{});
        co_await std::move(child);
        *maxSeen = std::max(*maxSeen, callbacksOn(*upstream));
    }
}

struct AwaitEntry {
    static constexpr const char* kName = "Await";
    static constexpr bool kOwnScope = false;

    struct Run {
        Run(std::shared_ptr<Probe> probe, const CancellationToken& upstream)
            : root(awaitLeaf(std::move(probe))) {
            root.setCancellationToken(upstream);
            root.resume();
        }

        bool unwound() { return root.isDone(); }
        void cancelOwnScope() {}

        Task<void> root;
    };

    static std::size_t repeat(int rounds, const CancellationToken& upstream) {
        std::size_t maxSeen = 0;
        auto root = awaitOwnTokenChildren(rounds, &upstream, &maxSeen);
        root.setCancellationToken(upstream);
        root.resume();
        return root.isDone() ? maxSeen : kNotDone;
    }
};

AsyncGenerator<int> generatorOverLeaf(std::shared_ptr<Probe> probe) {
    co_await parkedLeaf(std::move(probe));
    co_yield 1;
}

Task<void> consumeFirstValue(AsyncGenerator<int>* generator) {
    (void)co_await generator->next();
}

AsyncGenerator<int> countingProducer(int count) {
    for (int i = 0; i < count; ++i) {
        co_yield i;
    }
}

Task<void> drainObserving(AsyncGenerator<int>* generator, const CancellationToken* upstream,
                          std::size_t* maxSeen) {
    while (co_await generator->next()) {
        *maxSeen = std::max(*maxSeen, callbacksOn(*upstream));
    }
}

struct GeneratorEntry {
    static constexpr const char* kName = "Generator";
    static constexpr bool kOwnScope = false;

    struct Run {
        Run(std::shared_ptr<Probe> probe, const CancellationToken& upstream)
            : generator(generatorOverLeaf(std::move(probe))),
              consumer(consumeFirstValue(&generator)) {
            consumer.setCancellationToken(upstream);
            consumer.resume();
        }

        bool unwound() { return consumer.isDone(); }
        void cancelOwnScope() {}

        AsyncGenerator<int> generator;
        Task<void> consumer;
    };

    static std::size_t repeat(int rounds, const CancellationToken& upstream) {
        std::size_t maxSeen = 0;
        auto generator = countingProducer(rounds);
        generator.setCancellationToken(CancellationToken{});
        auto consumer = drainObserving(&generator, &upstream, &maxSeen);
        consumer.setCancellationToken(upstream);
        consumer.resume();
        return consumer.isDone() ? maxSeen : kNotDone;
    }
};

Task<void> whenAllOfLeaf(std::shared_ptr<Probe> probe,
                         std::shared_ptr<CompletionAwaiter<void>> failGate) {
    co_await slark::coro::when_all(parkedLeaf(std::move(probe)), failAfter(std::move(failGate)));
}

Task<void> whenAllRepeatedly(int rounds, const CancellationToken* upstream, std::size_t* maxSeen) {
    for (int i = 0; i < rounds; ++i) {
        co_await slark::coro::when_all(quickLeaf(), quickLeaf());
        *maxSeen = std::max(*maxSeen, callbacksOn(*upstream));
    }
}

struct WhenAllEntry {
    static constexpr const char* kName = "WhenAll";
    static constexpr bool kOwnScope = true;

    struct Run {
        Run(std::shared_ptr<Probe> probe, const CancellationToken& upstream)
            : failGate(std::make_shared<CompletionAwaiter<void>>()),
              root(whenAllOfLeaf(std::move(probe), failGate)) {
            root.setCancellationToken(upstream);
            root.resume();
        }

        bool unwound() { return root.isDone(); }
        void cancelOwnScope() { failGate->complete(); }

        std::shared_ptr<CompletionAwaiter<void>> failGate;
        Task<void> root;
    };

    static std::size_t repeat(int rounds, const CancellationToken& upstream) {
        std::size_t maxSeen = 0;
        auto root = whenAllRepeatedly(rounds, &upstream, &maxSeen);
        root.setCancellationToken(upstream);
        root.resume();
        return root.isDone() ? maxSeen : kNotDone;
    }
};

Task<int> whenAnyOfLeaf(AsyncScope* scope, std::shared_ptr<Probe> probe) {
    auto result = co_await slark::coro::when_any(*scope, parkedLeaf(std::move(probe)));
    co_return static_cast<int>(result.winnerIndex);
}

Task<void> whenAnyOfQuickLeaves(AsyncScope* scope) {
    (void)co_await slark::coro::when_any(*scope, quickLeaf(), quickLeaf());
}

struct WhenAnyEntry {
    static constexpr const char* kName = "WhenAny";
    static constexpr bool kOwnScope = true;

    struct Run {
        Run(std::shared_ptr<Probe> probe, const CancellationToken& upstream)
            : root(whenAnyOfLeaf(&scope, std::move(probe))) {
            root.setCancellationToken(upstream);
            root.resume();
        }

        bool unwound() { return root.isDone(); }
        void cancelOwnScope() { scope.cancelAll(); }

        AsyncScope scope;
        Task<int> root;
    };

    // Retired children are reclaimed when the outermost resume returns, so each round resumes alone.
    static std::size_t repeat(int rounds, const CancellationToken& upstream) {
        std::size_t maxSeen = 0;
        AsyncScope scope;
        for (int i = 0; i < rounds; ++i) {
            auto root = whenAnyOfQuickLeaves(&scope);
            root.setCancellationToken(upstream);
            root.resume();
            if (!root.isDone()) {
                return kNotDone;
            }
            maxSeen = std::max(maxSeen, callbacksOn(upstream));
        }
        return maxSeen;
    }
};

struct ScopeEntry {
    static constexpr const char* kName = "Scope";
    static constexpr bool kOwnScope = true;

    struct Run {
        Run(std::shared_ptr<Probe> probe, const CancellationToken& upstream) {
            scope.spawn(parkedLeaf(std::move(probe)), upstream);
        }

        bool unwound() { return slark::test::joinScope(scope); }
        void cancelOwnScope() { scope.cancelAll(); }

        AsyncScope scope;
    };

    static std::size_t repeat(int rounds, const CancellationToken& upstream) {
        std::size_t maxSeen = 0;
        AsyncScope scope;
        for (int i = 0; i < rounds; ++i) {
            scope.spawn(quickLeaf(), upstream);
            maxSeen = std::max(maxSeen, callbacksOn(upstream));
        }
        return maxSeen;
    }
};

struct JobEntry {
    static constexpr const char* kName = "Job";
    static constexpr bool kOwnScope = true;

    struct Run {
        Run(std::shared_ptr<Probe> probe, const CancellationToken& upstream)
            : job(slark::coro::launch(parkedLeaf(std::move(probe)),
                                      slark::coro::LaunchOptions{.token = upstream})) {}

        bool unwound() { return job.isDone(); }
        void cancelOwnScope() { job.cancel(); }

        slark::coro::Job<void> job;
    };

    static std::size_t repeat(int rounds, const CancellationToken& upstream) {
        std::size_t maxSeen = 0;
        for (int i = 0; i < rounds; ++i) {
            {
                auto job = slark::coro::launch(quickLeaf(),
                                               slark::coro::LaunchOptions{.token = upstream});
                (void)job;
            }
            maxSeen = std::max(maxSeen, callbacksOn(upstream));
        }
        return maxSeen;
    }
};

struct EntryNames {
    template <typename Entry>
    static std::string GetName(int) {
        return Entry::kName;
    }
};

template <typename Entry>
class TokenContractTest : public ::testing::Test {};

using Entries =
    ::testing::Types<AwaitEntry, GeneratorEntry, WhenAllEntry, WhenAnyEntry, ScopeEntry, JobEntry>;
TYPED_TEST_SUITE(TokenContractTest, Entries, EntryNames);

}  // namespace

TYPED_TEST(TokenContractTest, CancellingTheUpstreamTokenReachesTheLeaf) {
    CancellationToken upstream;
    auto probe = std::make_shared<Probe>();
    auto run = std::make_unique<typename TypeParam::Run>(probe, upstream);
    ASSERT_FALSE(probe->cancelled) << "the leaf is parked until something cancels it";
    ASSERT_FALSE(probe->completed);

    upstream.cancel();
    EXPECT_TRUE(probe->cancelled);
    EXPECT_TRUE(run->unwound());
}

TYPED_TEST(TokenContractTest, AnAlreadyCancelledUpstreamNeverParksTheLeaf) {
    CancellationToken upstream;
    upstream.cancel();
    auto probe = std::make_shared<Probe>();
    auto run = std::make_unique<typename TypeParam::Run>(probe, upstream);
    EXPECT_TRUE(probe->cancelled);
    EXPECT_FALSE(probe->completed);
    EXPECT_TRUE(run->unwound());
}

TYPED_TEST(TokenContractTest, CancellingTheOwnScopeDoesNotCancelTheUpstream) {
    if constexpr (TypeParam::kOwnScope) {
        CancellationToken upstream;
        auto probe = std::make_shared<Probe>();
        auto run = std::make_unique<typename TypeParam::Run>(probe, upstream);
        ASSERT_FALSE(probe->cancelled);

        run->cancelOwnScope();
        EXPECT_TRUE(probe->cancelled);
        EXPECT_FALSE(upstream.isCancelled()) << "a scope cancels downwards only";
        EXPECT_TRUE(run->unwound());
    } else {
        GTEST_SKIP() << "no scope of its own";
    }
}

TYPED_TEST(TokenContractTest, RepeatedUseDoesNotAccumulateRegistrations) {
    CancellationToken upstream;
    EXPECT_LE(TypeParam::repeat(64, upstream), 1u);
    EXPECT_EQ(callbacksOn(upstream), 0u);
}

TYPED_TEST(TokenContractTest, DestroyingTheEntryReleasesItsRegistrations) {
    CancellationToken upstream;
    const auto baseline = callbacksOn(upstream);
    {
        auto probe = std::make_shared<Probe>();
        auto run = std::make_unique<typename TypeParam::Run>(probe, upstream);
        ASSERT_GT(callbacksOn(upstream), baseline)
            << "a parked leaf must be reachable from the upstream token";
    }
    EXPECT_EQ(callbacksOn(upstream), baseline);
    EXPECT_FALSE(upstream.isCancelled());
}
