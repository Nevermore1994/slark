//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//
#include "Random.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <set>
#include <string>
#include <unordered_set>
#include <vector>

using namespace slark;

TEST(Random, IntegerRange) {
    for (int32_t i = 0; i < 100; ++i) {
        auto val = Random::random<int32_t>(0, 10);
        EXPECT_GE(val, 0);
        EXPECT_LE(val, 10);
    }
}

TEST(Random, IntegerSameMinMax) {
    for (int32_t i = 0; i < 10; ++i) {
        auto val = Random::random<int32_t>(5, 5);
        EXPECT_EQ(val, 5);
    }
}

TEST(Random, FloatingPointRange) {
    for (int32_t i = 0; i < 100; ++i) {
        auto val = Random::random<double>(0.0, 1.0);
        EXPECT_GE(val, 0.0);
        EXPECT_LE(val, 1.0);
    }
}

TEST(Random, FloatingPointNegativeRange) {
    for (int32_t i = 0; i < 100; ++i) {
        auto val = Random::random<double>(-10.0, -1.0);
        EXPECT_GE(val, -10.0);
        EXPECT_LE(val, -1.0);
    }
}

TEST(Random, U32Random) {
    std::set<uint32_t> values;
    for (int32_t i = 0; i < 100; ++i) {
        values.insert(Random::u32Random());
    }
    EXPECT_GT(values.size(), 1u);
}

TEST(Random, I32Random) {
    for (int32_t i = 0; i < 100; ++i) {
        auto val = Random::i32Random();
        EXPECT_GE(val, 0);
    }
}

TEST(Random, RandomStringLength) {
    auto str = Random::randomString(0);
    EXPECT_EQ(str.size(), 0u);

    str = Random::randomString(1);
    EXPECT_EQ(str.size(), 1u);

    str = Random::randomString(100);
    EXPECT_EQ(str.size(), 100u);
}

TEST(Random, RandomStringCharacters) {
    auto str = Random::randomString(1000);
    for (char c : str) {
        bool isAlphaNum =
            (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9');
        EXPECT_TRUE(isAlphaNum) << "Invalid character: " << c;
    }
}

TEST(Random, RandomStringUniqueness) {
    std::set<std::string> strings;
    for (int32_t i = 0; i < 100; ++i) {
        strings.insert(Random::randomString(16));
    }
    EXPECT_EQ(strings.size(), 100u);
}

TEST(Random, UniqueIdLength) {
    auto id = Random::uniqueId();
    EXPECT_EQ(id.size(), 64u);
}

TEST(Random, UniqueIdUniqueness) {
    std::set<std::string> ids;
    for (int32_t i = 0; i < 100; ++i) {
        ids.insert(Random::uniqueId());
    }
    EXPECT_EQ(ids.size(), 100u);
}

TEST(Random, UuidFormat) {
    auto id = Random::uuid();
    EXPECT_EQ(id.size(), 36u);
    EXPECT_EQ(id[8], '-');
    EXPECT_EQ(id[13], '-');
    EXPECT_EQ(id[14], '4');
    EXPECT_EQ(id[18], '-');
    EXPECT_EQ(id[23], '-');

    // variant bits: 8, 9, a, b
    char variant = id[19];
    EXPECT_TRUE(variant == '8' || variant == '9' || variant == 'a' || variant == 'b')
        << "Invalid variant: " << variant;
}

TEST(Random, UuidUniqueness) {
    std::set<std::string> uuids;
    for (int32_t i = 0; i < 100; ++i) {
        uuids.insert(Random::uuid());
    }
    EXPECT_EQ(uuids.size(), 100u);
}

TEST(Random, UuidHexCharacters) {
    auto id = Random::uuid();
    for (size_t i = 0; i < id.size(); ++i) {
        if (i == 8 || i == 13 || i == 18 || i == 23) {
            EXPECT_EQ(id[i], '-');
        } else {
            bool isHex = (id[i] >= '0' && id[i] <= '9') || (id[i] >= 'a' && id[i] <= 'f');
            EXPECT_TRUE(isHex) << "Invalid hex char at pos " << i << ": " << id[i];
        }
    }
}

TEST(Random, RandomSelectAll) {
    std::vector<int32_t> source = {1, 2, 3, 4, 5};
    auto result = Random::randomSelect(source, 10);
    EXPECT_EQ(result.size(), source.size());
    std::sort(result.begin(), result.end());
    std::vector<int32_t> expected = {1, 2, 3, 4, 5};
    EXPECT_EQ(result, expected);
}

TEST(Random, RandomSelectSubset) {
    std::vector<int32_t> source = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
    auto result = Random::randomSelect(source, 3);
    EXPECT_EQ(result.size(), 3u);

    for (auto val : result) {
        EXPECT_TRUE(std::find(source.begin(), source.end(), val) != source.end());
    }
}

TEST(Random, RandomSelectEmpty) {
    std::vector<int32_t> source;
    auto result = Random::randomSelect(source, 5);
    EXPECT_TRUE(result.empty());
}

TEST(Random, RandomSelectZeroCount) {
    std::vector<int32_t> source = {1, 2, 3};
    auto result = Random::randomSelect(source, 0);
    EXPECT_TRUE(result.empty());
}

TEST(Random, Int64Range) {
    for (int32_t i = 0; i < 100; ++i) {
        auto val = Random::random<int64_t>(-1000, 1000);
        EXPECT_GE(val, -1000);
        EXPECT_LE(val, 1000);
    }
}
