//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#include "ConnectionFactory.h"

#include <gtest/gtest.h>

using namespace slark;
using namespace slark::http;
using namespace std::chrono_literals;

TEST(ConnectBudgetTest, SplitsRemainingAcrossCandidates) {
    EXPECT_EQ(connectAttemptBudget(10s, 2), 5s);
    EXPECT_EQ(connectAttemptBudget(3s, 3), 1s);
    EXPECT_EQ(connectAttemptBudget(10s, 1), 10s);
}

TEST(ConnectBudgetTest, FloorApplies) {
    EXPECT_EQ(connectAttemptBudget(1200ms, 2), 1s);
    EXPECT_EQ(connectAttemptBudget(1s, 2), 1s);
}

TEST(ConnectBudgetTest, NeverExceedsRemaining) {
    EXPECT_EQ(connectAttemptBudget(400ms, 2), 400ms);
    EXPECT_EQ(connectAttemptBudget(0ms, 2), 0ms);
    EXPECT_EQ(connectAttemptBudget(500ms, 0), 500ms);
}
