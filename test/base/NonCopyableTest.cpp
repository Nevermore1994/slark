//
// Created by Nevermore on 2024/01/01.
// Copyright (c) 2024 Nevermore All rights reserved.
//
#include "NonCopyable.h"

#include "gtest/gtest.h"

namespace slark::test {

class CopyableOnly : public NonCopyable {
public:
    CopyableOnly() = default;

    explicit CopyableOnly(int32_t v) : value(v) {}

    int32_t value = 0;
};

class NoCopyNoMove : public NonCopyMovable {
public:
    NoCopyNoMove() = default;

    explicit NoCopyNoMove(int32_t v) : value(v) {}

    int32_t value = 0;
};

TEST(NonCopyableTest, AllowMove) {
    static_assert(!std::is_copy_constructible_v<CopyableOnly>);
    static_assert(!std::is_copy_assignable_v<CopyableOnly>);
    static_assert(std::is_move_constructible_v<CopyableOnly>);
    static_assert(std::is_move_assignable_v<CopyableOnly>);

    CopyableOnly a(42);
    CopyableOnly b(std::move(a));
    EXPECT_EQ(b.value, 42);

    CopyableOnly c;
    c = std::move(b);
    EXPECT_EQ(c.value, 42);
}

TEST(NonCopyMovableTest, DisallowCopyAndMove) {
    static_assert(!std::is_copy_constructible_v<NoCopyNoMove>);
    static_assert(!std::is_copy_assignable_v<NoCopyNoMove>);
    static_assert(!std::is_move_constructible_v<NoCopyNoMove>);
    static_assert(!std::is_move_assignable_v<NoCopyNoMove>);

    NoCopyNoMove a(42);
    EXPECT_EQ(a.value, 42);
}

} // namespace slark::test
