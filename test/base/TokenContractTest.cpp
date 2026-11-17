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
#include <string_view>

#include "AsyncGenerator.hpp"
#include "AsyncIOOperation.hpp"
#include "Awaitable.hpp"
#include "CancellationToken.h"
#include "CoScope.hpp"
#include "CoTimeout.hpp"
#include "CoWhenAll.hpp"
#include "CoroutineControl.hpp"
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

class ParkedIo final : public slark::AsyncIOOperationBase<ParkedIo, void> {
protected:
    void startOperation() noexcept override {}
};

struct TimerLeaf {
    static constexpr std::string_view kName = "Timer";

    static Task<void> park(std::shared_ptr<Probe> probe) {
        try {
            co_await TimerAwaiter(5000ms);
            probe->completed = true;
        } catch (const CancelledException&) {
            probe->cancelled = true;
            throw;
        }
    }
};

struct IoLeaf {
    static constexpr std::string_view kName = "Io";

    static Task<void> park(std::shared_ptr<Probe> probe) {
        try {
            (void)co_await ParkedIo{};
            probe->completed = true;
        } catch (const CancelledException&) {
            probe->cancelled = true;
            throw;
        }
    }
};

template <typename Leaf>
Task<void> parkedLeaf(std::shared_ptr<Probe> probe) {
    return Leaf::park(std::move(probe));
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

template <typename Leaf>
Task<void> awaitLeaf(std::shared_ptr<Probe> probe) {
    co_await parkedLeaf<Leaf>(std::move(probe));
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

template <typename Leaf>
struct AwaitEntry {
    static std::string name() {
        return "Await_" + std::string(Leaf::kName);
    }
    static constexpr bool kOwnScope = false;
    static constexpr bool kEntersLeaf = true;

    struct Run {
        Run(std::shared_ptr<Probe> probe, const CancellationToken& upstream)
            : root(awaitLeaf<Leaf>(std::move(probe))) {
            root.attach(slark::SpawnEdge{}, &upstream);
            root.resume();
        }

        bool unwound() { return root.isDone(); }
        void cancelOwnScope() {}

        Task<void> root;
    };

    static std::size_t repeat(int rounds, const CancellationToken& upstream) {
        std::size_t maxSeen = 0;
        auto root = awaitOwnTokenChildren(rounds, &upstream, &maxSeen);
        root.attach(slark::SpawnEdge{}, &upstream);
        root.resume();
        return root.isDone() ? maxSeen : kNotDone;
    }
};

template <typename Leaf>
AsyncGenerator<int> generatorOverLeaf(std::shared_ptr<Probe> probe) {
    co_await parkedLeaf<Leaf>(std::move(probe));
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

template <typename Leaf>
struct GeneratorEntry {
    static std::string name() {
        return "Generator_" + std::string(Leaf::kName);
    }
    static constexpr bool kOwnScope = false;
    static constexpr bool kEntersLeaf = true;

    struct Run {
        Run(std::shared_ptr<Probe> probe, const CancellationToken& upstream)
            : generator(generatorOverLeaf<Leaf>(std::move(probe))),
              consumer(consumeFirstValue(&generator)) {
            consumer.attach(slark::SpawnEdge{}, &upstream);
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
        consumer.attach(slark::SpawnEdge{}, &upstream);
        consumer.resume();
        return consumer.isDone() ? maxSeen : kNotDone;
    }
};

template <typename Leaf>
Task<void> whenAllOfLeaf(std::shared_ptr<Probe> probe,
                         std::shared_ptr<CompletionAwaiter<void>> failGate) {
    co_await slark::coro::when_all(parkedLeaf<Leaf>(std::move(probe)),
                                   failAfter(std::move(failGate)));
}

Task<void> whenAllRepeatedly(int rounds, const CancellationToken* upstream, std::size_t* maxSeen) {
    for (int i = 0; i < rounds; ++i) {
        co_await slark::coro::when_all(quickLeaf(), quickLeaf());
        *maxSeen = std::max(*maxSeen, callbacksOn(*upstream));
    }
}

template <typename Leaf>
struct WhenAllEntry {
    static std::string name() {
        return "WhenAll_" + std::string(Leaf::kName);
    }
    static constexpr bool kOwnScope = true;
    static constexpr bool kEntersLeaf = true;

    struct Run {
        Run(std::shared_ptr<Probe> probe, const CancellationToken& upstream)
            : failGate(std::make_shared<CompletionAwaiter<void>>()),
              root(whenAllOfLeaf<Leaf>(std::move(probe), failGate)) {
            root.attach(slark::SpawnEdge{}, &upstream);
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
        root.attach(slark::SpawnEdge{}, &upstream);
        root.resume();
        return root.isDone() ? maxSeen : kNotDone;
    }
};

template <typename Leaf>
Task<int> whenAnyOfLeaf(AsyncScope* scope, std::shared_ptr<Probe> probe) {
    auto result = co_await slark::coro::when_any(*scope, parkedLeaf<Leaf>(std::move(probe)));
    co_return static_cast<int>(result.winnerIndex);
}

Task<void> whenAnyOfQuickLeaves(AsyncScope* scope) {
    (void)co_await slark::coro::when_any(*scope, quickLeaf(), quickLeaf());
}

template <typename Leaf>
struct WhenAnyEntry {
    static std::string name() {
        return "WhenAny_" + std::string(Leaf::kName);
    }
    static constexpr bool kOwnScope = true;
    static constexpr bool kEntersLeaf = true;

    struct Run {
        Run(std::shared_ptr<Probe> probe, const CancellationToken& upstream)
            : root(whenAnyOfLeaf<Leaf>(&scope, std::move(probe))) {
            root.attach(slark::SpawnEdge{}, &upstream);
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
            root.attach(slark::SpawnEdge{}, &upstream);
            root.resume();
            if (!root.isDone()) {
                return kNotDone;
            }
            maxSeen = std::max(maxSeen, callbacksOn(upstream));
        }
        return maxSeen;
    }
};

template <typename Leaf>
struct ScopeEntry {
    static std::string name() {
        return "Scope_" + std::string(Leaf::kName);
    }
    static constexpr bool kOwnScope = true;
    static constexpr bool kEntersLeaf = true;

    struct Run {
        Run(std::shared_ptr<Probe> probe, const CancellationToken& upstream) {
            scope.spawn(parkedLeaf<Leaf>(std::move(probe)), upstream);
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

template <typename Leaf>
struct JobEntry {
    static std::string name() {
        return "Job_" + std::string(Leaf::kName);
    }
    static constexpr bool kOwnScope = true;
    static constexpr bool kEntersLeaf = true;

    struct Run {
        Run(std::shared_ptr<Probe> probe, const CancellationToken& upstream)
            : job(slark::coro::launch(parkedLeaf<Leaf>(std::move(probe)),
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

template <typename Leaf>
Task<void> timedLeaf(std::shared_ptr<Probe> probe) {
    co_await slark::coro::with_timeout(parkedLeaf<Leaf>(std::move(probe)), 60s);
}

Task<void> timedRepeatedly(int rounds, const CancellationToken* upstream, std::size_t* maxSeen) {
    for (int i = 0; i < rounds; ++i) {
        co_await slark::coro::with_timeout(quickLeaf(), 60s);
        *maxSeen = std::max(*maxSeen, callbacksOn(*upstream));
    }
}

template <typename Leaf>
struct WithTimeoutEntry {
    static std::string name() {
        return "WithTimeout_" + std::string(Leaf::kName);
    }
    static constexpr bool kOwnScope = false;
    // with_timeout refuses a cancelled upstream before its child, and so the leaf, ever starts.
    static constexpr bool kEntersLeaf = false;

    struct Run {
        Run(std::shared_ptr<Probe> probe, const CancellationToken& upstream)
            : root(timedLeaf<Leaf>(std::move(probe))) {
            root.attach(slark::SpawnEdge{}, &upstream);
            root.resume();
        }

        bool unwound() { return root.isDone(); }
        void cancelOwnScope() {}

        Task<void> root;
    };

    static std::size_t repeat(int rounds, const CancellationToken& upstream) {
        std::size_t maxSeen = 0;
        auto root = timedRepeatedly(rounds, &upstream, &maxSeen);
        root.attach(slark::SpawnEdge{}, &upstream);
        root.resume();
        return root.isDone() ? maxSeen : kNotDone;
    }
};

struct EntryNames {
    template <typename Entry>
    static std::string GetName(int) {
        return Entry::name();
    }
};

template <typename Entry>
class TokenContractTest : public ::testing::Test {};

using Entries = ::testing::Types<AwaitEntry<TimerLeaf>,     AwaitEntry<IoLeaf>,
                                 GeneratorEntry<TimerLeaf>, GeneratorEntry<IoLeaf>,
                                 WhenAllEntry<TimerLeaf>,   WhenAllEntry<IoLeaf>,
                                 WhenAnyEntry<TimerLeaf>,   WhenAnyEntry<IoLeaf>,
                                 ScopeEntry<TimerLeaf>,     ScopeEntry<IoLeaf>,
                                 JobEntry<TimerLeaf>,       JobEntry<IoLeaf>,
                                 WithTimeoutEntry<TimerLeaf>, WithTimeoutEntry<IoLeaf>>;
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
    if constexpr (TypeParam::kEntersLeaf) {
        EXPECT_TRUE(probe->cancelled) << "the leaf must observe the cancellation instead of parking";
    } else {
        EXPECT_FALSE(probe->cancelled) << "the entry refuses before it enters the leaf";
    }
    EXPECT_FALSE(probe->completed);
    EXPECT_TRUE(run->unwound()) << "the entry must unwind without waiting on the leaf";
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

TEST(TokenEdgeGuardTest, AnAttachedFrameWithoutTokenRunsToCompletion) {
    auto body = slark::coro::detail::runJobBody<void>(
        std::make_shared<slark::coro::detail::JobState<void>>(), quickLeaf());
    slark::attach(body.getHandle().promise(), slark::SpawnEdge{}, nullptr);
    body.resume();
    EXPECT_TRUE(body.isDone());
}

TEST(TokenEdgeGuardTest, AttachingAFrameTwiceIsRejected) {
    auto body = slark::coro::detail::runJobBody<void>(
        std::make_shared<slark::coro::detail::JobState<void>>(), quickLeaf());
    slark::attach(body.getHandle().promise(), slark::SpawnEdge{}, nullptr);
    EXPECT_THROW(slark::attach(body.getHandle().promise(), slark::SpawnEdge{}, nullptr),
                 std::logic_error);
}

constexpr auto kGuardMessage = "composed coroutine started without a cancellation edge";

namespace {

Task<void> cancelOwnTokenThenPark() {
    auto token = co_await slark::coro::cancellation_token();
    token->cancel();
    try {
        co_await TimerAwaiter(5000ms);
    } catch (const CancelledException&) {
    }
}

}  // namespace

TEST(TokenEdgeGuardTest, RootAttachedToASourceIsCancelledBySourceButNeverCancelsIt) {
    CancellationToken source;
    auto root = cancelOwnTokenThenPark();
    root.attach(slark::SpawnEdge{}, &source);
    root.resume();
    EXPECT_TRUE(root.isDone());
    EXPECT_FALSE(source.isCancelled()) << "a root cancels only its own token";
}

TEST(TokenEdgeGuardTest, GeneratorRootAttachedToASourceIsCancelledBySource) {
    CancellationToken source;
    auto probe = std::make_shared<Probe>();
    auto generator = generatorOverLeaf<TimerLeaf>(probe);
    generator.attach(slark::SpawnEdge{}, &source);
    auto consumer = consumeFirstValue(&generator);
    consumer.resume();
    ASSERT_FALSE(probe->cancelled);
    source.cancel();
    EXPECT_TRUE(probe->cancelled);
}

TEST(TokenEdgeGuardDeathTest, UnattachedComposedFrameAbortsBeforeItsBodyRuns) {
    testing::FLAGS_gtest_death_test_style = "threadsafe";
    EXPECT_DEATH(
        {
            auto body = slark::coro::detail::runJobBody<void>(
                std::make_shared<slark::coro::detail::JobState<void>>(), quickLeaf());
            body.resume();
        },
        kGuardMessage);
}

TEST(TokenEdgeGuardDeathTest, HandWrittenBindingDoesNotSatisfyTheGuard) {
    testing::FLAGS_gtest_death_test_style = "threadsafe";
    EXPECT_DEATH(
        {
            auto body = slark::coro::detail::runJobBody<void>(
                std::make_shared<slark::coro::detail::JobState<void>>(), quickLeaf());
            body.setCancellationToken(CancellationToken{});
            body.resume();
        },
        kGuardMessage);
}

TEST(TokenEdgeGuardDeathTest, UnattachedScopeChildAbortsBeforeItsBodyRuns) {
    testing::FLAGS_gtest_death_test_style = "threadsafe";
    EXPECT_DEATH(
        {
            auto child = slark::coro::detail::scopeWrapper<void>(
                std::make_shared<slark::coro::detail::ScopeCore>(), 0, quickLeaf());
            child.resume();
        },
        kGuardMessage);
}
