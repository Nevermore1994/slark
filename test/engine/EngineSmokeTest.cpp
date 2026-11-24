//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#include <gtest/gtest.h>

#include <type_traits>

#include "MediaDefs.h"
#include "EngineTypes.h"

namespace eng = slark::engine;

TEST(EngineSmokeTest, StatusEnumHasSevenValuesAndIdleIsZero) {
    EXPECT_EQ(static_cast<int>(eng::Status::Idle), 0);
    EXPECT_EQ(static_cast<int>(eng::Status::Failed), 6);
}

TEST(EngineSmokeTest, InvalidSeekEpochIsZeroAndSeekEpochIs64Bit) {
    EXPECT_EQ(eng::kInvalidSeekEpoch, 0u);
    static_assert(std::is_same_v<eng::SeekEpoch, uint64_t>);
}

TEST(EngineSmokeTest, EngineReusesExistingTrackType) {
    // engine 不重复定义 TrackType，直接使用 slark::TrackType（MediaDefs.h）。
    static_assert(std::is_enum_v<slark::TrackType>);
    EXPECT_NE(slark::TrackType::Audio, slark::TrackType::Video);
}
