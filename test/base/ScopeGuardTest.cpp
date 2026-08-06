//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//
#include "ScopeGuard.h"

#include <gtest/gtest.h>

using namespace slark;

TEST(ScopeGuard, ExecutesOnDestroy) {
    bool called = false;
    {
        ScopeGuard guard([&called] {
            called = true;
        });
        EXPECT_FALSE(called);
    }
    EXPECT_TRUE(called);
}

TEST(ScopeGuard, DismissPreventsExecution) {
    bool called = false;
    {
        ScopeGuard guard([&called] {
            called = true;
        });
        guard.dismiss();
    }
    EXPECT_FALSE(called);
}

TEST(ScopeGuard, ReleaseExecutesImmediately) {
    bool called = false;
    {
        ScopeGuard guard([&called] {
            called = true;
        });
        EXPECT_FALSE(called);
        guard.release();
        EXPECT_TRUE(called);
    }
    EXPECT_TRUE(called);
}

TEST(ScopeGuard, ReleaseIdempotent) {
    int32_t count = 0;
    {
        ScopeGuard guard([&count] {
            ++count;
        });
        guard.release();
        guard.release();
    }
    EXPECT_EQ(count, 1);
}

TEST(ScopeGuard, DismissAfterRelease) {
    int32_t count = 0;
    {
        ScopeGuard guard([&count] {
            ++count;
        });
        guard.release();
        guard.dismiss();
    }
    EXPECT_EQ(count, 1);
}

TEST(ScopeGuard, EarlyReturnScenario) {
    bool cleaned = false;
    auto doWork = [&](bool fail) {
        ScopeGuard guard([&cleaned] {
            cleaned = true;
        });
        if (fail) {
            return;
        }
        guard.dismiss();
    };

    doWork(true);
    EXPECT_TRUE(cleaned);

    cleaned = false;
    doWork(false);
    EXPECT_FALSE(cleaned);
}

TEST(ScopeGuard, LambdaCapture) {
    std::vector<int32_t> log;
    {
        ScopeGuard g1([&log] {
            log.push_back(1);
        });
        ScopeGuard g2([&log] {
            log.push_back(2);
        });
    }
    ASSERT_EQ(log.size(), 2u);
    EXPECT_EQ(log[0], 2);
    EXPECT_EQ(log[1], 1);
}
