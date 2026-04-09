
#include "Range.h"

#include <gtest/gtest.h>

using namespace slark;

TEST(Range, DefaultConstruction) {
    Range r;
    EXPECT_FALSE(r.isValid());
    EXPECT_EQ(r.start(), 0u);
    EXPECT_EQ(r.end(), 0u);
}

TEST(Range, ConstructWithPosAndSize) {
    Range r(10, 20);
    EXPECT_TRUE(r.isValid());
    EXPECT_EQ(r.start(), 10u);
    EXPECT_EQ(r.end(), 29u);
}

TEST(Range, ConstructWithPosOnly) {
    Range r(5u);
    EXPECT_TRUE(r.isValid());
    EXPECT_EQ(r.start(), 5u);
    EXPECT_EQ(r.end(), INT64_MAX);
    EXPECT_FALSE(r.size.has_value());
}

TEST(Range, MoveTo) {
    Range r(10, 20);
    r.moveTo(50);
    EXPECT_EQ(r.start(), 50u);
    EXPECT_EQ(r.end(), 69u);
}

TEST(Range, ShiftPositive) {
    Range r(10, 5);
    r.shift(3);
    EXPECT_EQ(r.start(), 13u);
}

TEST(Range, ShiftNegative) {
    Range r(10, 5);
    r.shift(-3);
    EXPECT_EQ(r.start(), 7u);
}

TEST(Range, ShiftNegativeClampToZero) {
    Range r(5, 10);
    r.shift(-100);
    EXPECT_EQ(r.start(), 0u);
}

TEST(Range, ShiftFromInvalid) {
    Range r;
    r.shift(10);
    EXPECT_EQ(r.start(), 10u);
}

TEST(Range, ContainsValid) {
    Range r(10, 20);
    EXPECT_TRUE(r.contains(10));
    EXPECT_TRUE(r.contains(15));
    EXPECT_TRUE(r.contains(29));
    EXPECT_FALSE(r.contains(9));
    EXPECT_FALSE(r.contains(30));
}

TEST(Range, ContainsInvalid) {
    Range r;
    EXPECT_FALSE(r.contains(0));
    EXPECT_FALSE(r.contains(100));
}

TEST(Range, ContainsOpenEnd) {
    Range r(10u);
    EXPECT_TRUE(r.contains(10));
    EXPECT_TRUE(r.contains(1000000));
    EXPECT_FALSE(r.contains(9));
}

TEST(Range, OverlapNormal) {
    Range r1(10, 20);
    Range r2(20, 30);
    auto result = r1.overlap(r2);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->start(), 20u);
    EXPECT_EQ(result->end(), 29u);
}

TEST(Range, OverlapNoIntersection) {
    Range r1(10, 5);
    Range r2(20, 5);
    auto result = r1.overlap(r2);
    EXPECT_FALSE(result.has_value());
}

TEST(Range, OverlapFullContainment) {
    Range r1(10, 50);
    Range r2(20, 10);
    auto result = r1.overlap(r2);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->start(), 20u);
    EXPECT_EQ(result->end(), 29u);
}

TEST(Range, OverlapWithInvalid) {
    Range r1;
    Range r2(10, 20);
    EXPECT_FALSE(r1.overlap(r2).has_value());
    EXPECT_FALSE(r2.overlap(r1).has_value());
}

TEST(Range, OverlapBothInvalid) {
    Range r1;
    Range r2;
    EXPECT_FALSE(r1.overlap(r2).has_value());
}

TEST(Range, OverlapOpenEnd) {
    Range r1(10u);
    Range r2(20, 30);
    auto result = r1.overlap(r2);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->start(), 20u);
    EXPECT_EQ(result->end(), 49u);
}

TEST(Range, ComparisonEqual) {
    Range r1(10, 20);
    Range r2(10, 20);
    EXPECT_EQ(r1, r2);
    EXPECT_TRUE((r1 <=> r2) == std::strong_ordering::equal);
}

TEST(Range, ComparisonLess) {
    Range r1(5, 10);
    Range r2(10, 10);
    EXPECT_TRUE(r1 < r2);
}

TEST(Range, ComparisonGreater) {
    Range r1(20, 10);
    Range r2(10, 10);
    EXPECT_TRUE(r1 > r2);
}

TEST(Range, ComparisonSameStartDifferentEnd) {
    Range r1(10, 5);
    Range r2(10, 20);
    EXPECT_TRUE(r1 < r2);
}

TEST(Range, ComparisonBothInvalid) {
    Range r1;
    Range r2;
    EXPECT_EQ(r1, r2);
}

TEST(Range, ComparisonInvalidLessThanValid) {
    Range r1;
    Range r2(10, 20);
    EXPECT_TRUE(r1 < r2);
}

TEST(Range, ComparisonValidGreaterThanInvalid) {
    Range r1(10, 20);
    Range r2;
    EXPECT_TRUE(r1 > r2);
}

TEST(Range, ToStringInvalid) {
    Range r;
    EXPECT_EQ(r.toString(), "Invalid Range");
}

TEST(Range, ToStringWithSize) {
    Range r(10, 20);
    auto str = r.toString();
    EXPECT_NE(str.find("10"), std::string::npos);
    EXPECT_NE(str.find("29"), std::string::npos);
}

TEST(Range, ToStringOpenEnd) {
    Range r(10u);
    auto str = r.toString();
    EXPECT_NE(str.find("10"), std::string::npos);
    EXPECT_NE(str.find("end"), std::string::npos);
}

TEST(Range, ToHeaderStringInvalid) {
    Range r;
    EXPECT_EQ(r.toHeaderString(), "bytes=0-");
}

TEST(Range, ToHeaderStringOpenEnd) {
    Range r(100u);
    EXPECT_EQ(r.toHeaderString(), "bytes=100-");
}

TEST(Range, ToHeaderStringWithSize) {
    Range r(0, 500);
    EXPECT_EQ(r.toHeaderString(), "bytes=0-499");
}

TEST(Range, EndWithSizeOne) {
    Range r(10, 1);
    EXPECT_EQ(r.start(), 10u);
    EXPECT_EQ(r.end(), 10u);
    EXPECT_TRUE(r.contains(10));
    EXPECT_FALSE(r.contains(11));
}

TEST(Range, OverlapAdjacentRanges) {
    Range r1(10, 10);
    Range r2(20, 10);
    auto result = r1.overlap(r2);
    EXPECT_FALSE(result.has_value());
}

TEST(Range, OverlapSinglePointIntersection) {
    Range r1(10, 11);
    Range r2(20, 10);
    auto result = r1.overlap(r2);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->start(), 20u);
    EXPECT_EQ(result->end(), 20u);
}
