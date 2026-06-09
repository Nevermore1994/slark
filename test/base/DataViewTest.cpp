//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//
#include "DataView.hpp"

#include <array>
#include <gtest/gtest.h>
#include <string>
#include <vector>

using namespace slark;

TEST(DataView, DefaultConstruction) {
    DataView view;
    EXPECT_TRUE(view.empty());
    EXPECT_EQ(view.size(), 0u);
    EXPECT_EQ(view.data(), nullptr);
}

TEST(DataView, ConstructFromStringView) {
    DataView view("hello");
    EXPECT_FALSE(view.empty());
    EXPECT_EQ(view.size(), 5u);
    EXPECT_EQ(view.toStringView(), "hello");
}

TEST(DataView, ConstructFromPointerAndSize) {
    std::string str = "world";
    DataView view(reinterpret_cast<const Byte*>(str.data()), str.size());
    EXPECT_EQ(view.size(), 5u);
    EXPECT_EQ(view.toStringView(), "world");
}

TEST(DataView, FromString) {
    auto view = DataView::fromString("test");
    EXPECT_EQ(view.toStringView(), "test");
}

TEST(DataView, FromCString) {
    auto view = DataView::fromCString("hello");
    EXPECT_EQ(view.size(), 5u);
    EXPECT_EQ(view.toStringView(), "hello");
}

TEST(DataView, FromArray) {
    std::array<Byte, 3> arr = {0x41, 0x42, 0x43};
    auto view = DataView::fromArray(arr);
    EXPECT_EQ(view.size(), 3u);
    EXPECT_EQ(view.toStringView(), "ABC");
}

TEST(DataView, UserDefinedLiteral) {
    auto view = "hello"_view;
    EXPECT_EQ(view.size(), 5u);
    EXPECT_EQ(view.toStringView(), "hello");
}

TEST(DataView, ElementAccess) {
    auto view = "abcd"_view;
    EXPECT_EQ(static_cast<char>(view[0]), 'a');
    EXPECT_EQ(static_cast<char>(view[3]), 'd');
    EXPECT_EQ(static_cast<char>(view.front()), 'a');
    EXPECT_EQ(static_cast<char>(view.back()), 'd');
}

TEST(DataView, AtSafe) {
    auto view = "abc"_view;
    auto val = view.at(0);
    ASSERT_TRUE(val.has_value());
    EXPECT_EQ(static_cast<char>(*val), 'a');

    auto outOfRange = view.at(100);
    EXPECT_FALSE(outOfRange.has_value());
}

TEST(DataView, Subview) {
    auto view = "hello world"_view;
    auto sub = view.subview(6);
    EXPECT_EQ(sub.toStringView(), "world");

    auto sub2 = view.subview(0, 5);
    EXPECT_EQ(sub2.toStringView(), "hello");
}

TEST(DataView, SubviewOutOfRange) {
    auto view = "hello"_view;
    auto sub = view.subview(100);
    EXPECT_TRUE(sub.empty());
}

TEST(DataView, First) {
    auto view = "hello world"_view;
    auto first5 = view.first(5);
    EXPECT_EQ(first5.toStringView(), "hello");
}

TEST(DataView, FirstExceedsSize) {
    auto view = "hi"_view;
    auto result = view.first(100);
    EXPECT_EQ(result.toStringView(), "hi");
}

TEST(DataView, Last) {
    auto view = "hello world"_view;
    auto last5 = view.last(5);
    EXPECT_EQ(last5.toStringView(), "world");
}

TEST(DataView, LastExceedsSize) {
    auto view = "hi"_view;
    auto result = view.last(100);
    EXPECT_EQ(result.toStringView(), "hi");
}

TEST(DataView, ToString) {
    auto view = "hello"_view;
    std::string str = view.toString();
    EXPECT_EQ(str, "hello");
}

TEST(DataView, StartsWith) {
    auto view = "hello world"_view;
    EXPECT_TRUE(view.startsWith("hello"_view));
    EXPECT_FALSE(view.startsWith("world"_view));
    EXPECT_TRUE(view.startsWith(DataView{}));
}

TEST(DataView, EndsWith) {
    auto view = "hello world"_view;
    EXPECT_TRUE(view.endsWith("world"_view));
    EXPECT_FALSE(view.endsWith("hello"_view));
    EXPECT_TRUE(view.endsWith(DataView{}));
}

TEST(DataView, Contains) {
    auto view = "hello world"_view;
    EXPECT_TRUE(view.contains("lo wo"_view));
    EXPECT_TRUE(view.contains("hello"_view));
    EXPECT_FALSE(view.contains("xyz"_view));
    EXPECT_TRUE(view.contains(DataView{}));
}

TEST(DataView, ContainsLargerNeedle) {
    auto view = "hi"_view;
    EXPECT_FALSE(view.contains("hello world"_view));
}

TEST(DataView, FindByte) {
    auto view = "hello"_view;
    EXPECT_EQ(view.find(static_cast<Byte>('l')), 2u);
    EXPECT_EQ(view.find(static_cast<Byte>('z')), DataView::npos);
}

TEST(DataView, FindChar) {
    auto view = "hello"_view;
    EXPECT_EQ(view.find('l'), 2u);
    EXPECT_EQ(view.find('z'), DataView::npos);
}

TEST(DataView, FindDataView) {
    auto view = "hello world"_view;
    EXPECT_EQ(view.find("world"_view), 6u);
    EXPECT_EQ(view.find("xyz"_view), DataView::npos);
    EXPECT_EQ(view.find(DataView{}, 0), 0u);
}

TEST(DataView, FindStringView) {
    auto view = "hello world"_view;
    EXPECT_EQ(view.find(std::string_view("world")), 6u);
}

TEST(DataView, FindWithOffset) {
    auto view = "abcabc"_view;
    EXPECT_EQ(view.find('a', 0), 0u);
    EXPECT_EQ(view.find('a', 1), 3u);
    EXPECT_EQ(view.find('a', 4), DataView::npos);
}

TEST(DataView, Rfind) {
    auto view = "abcabc"_view;
    EXPECT_EQ(view.rfind(static_cast<Byte>('a')), 3u);
    EXPECT_EQ(view.rfind(static_cast<Byte>('c')), 5u);
    EXPECT_EQ(view.rfind(static_cast<Byte>('z')), DataView::npos);
}

TEST(DataView, RfindWithPos) {
    auto view = "abcabc"_view;
    EXPECT_EQ(view.rfind(static_cast<Byte>('a'), 2), 0u);
}

TEST(DataView, RfindEmpty) {
    DataView view;
    EXPECT_EQ(view.rfind(static_cast<Byte>('a')), DataView::npos);
}

TEST(DataView, Compare) {
    auto view = "hello world"_view;
    EXPECT_EQ(view.compare(0, 5, "hello"_view), 0);
    EXPECT_LT(view.compare(0, 5, "hellz"_view), 0);
    EXPECT_GT(view.compare(0, 5, "hella"_view), 0);
}

TEST(DataView, CompareStringView) {
    auto view = "hello world"_view;
    EXPECT_EQ(view.compare(6, 5, std::string_view("world")), 0);
}

TEST(DataView, CompareEmpty) {
    DataView view;
    EXPECT_EQ(view.compare(0, 0, DataView{}), 0);
    EXPECT_LT(view.compare(0, 0, "a"_view), 0);
}

TEST(DataView, StartWith) {
    auto view = "hello world"_view;
    EXPECT_TRUE(view.startWith("hello"));
    EXPECT_FALSE(view.startWith("world"));
}

TEST(DataView, Substr) {
    auto view = "hello world"_view;
    auto sub = view.substr(6);
    EXPECT_EQ(sub.toStringView(), "world");

    auto sub2 = view.substr(0, 5);
    EXPECT_EQ(sub2.toStringView(), "hello");
}

TEST(DataView, Split) {
    auto view = "a,b,c,d"_view;
    auto parts = view.split(",");
    ASSERT_EQ(parts.size(), 4u);
    EXPECT_EQ(parts[0].toStringView(), "a");
    EXPECT_EQ(parts[1].toStringView(), "b");
    EXPECT_EQ(parts[2].toStringView(), "c");
    EXPECT_EQ(parts[3].toStringView(), "d");
}

TEST(DataView, RemoveSpace) {
    auto view = " h e l l o "_view;
    auto result = view.removeSpace();
    EXPECT_EQ(result, "hello");
}

TEST(DataView, RemovePrefix) {
    auto view = "///path"_view;
    view.removePrefix('/');
    EXPECT_EQ(view.toStringView(), "path");
}

TEST(DataView, RemovePrefixNoMatch) {
    auto view = "hello"_view;
    view.removePrefix('/');
    EXPECT_EQ(view.toStringView(), "hello");
}

TEST(DataView, Equality) {
    auto a = "hello"_view;
    auto b = "hello"_view;
    auto c = "world"_view;
    EXPECT_EQ(a, b);
    EXPECT_NE(a, c);
}

TEST(DataView, Ordering) {
    auto a = "abc"_view;
    auto b = "abd"_view;
    EXPECT_TRUE(a < b);
    EXPECT_TRUE(b > a);
}

TEST(DataView, Iterators) {
    auto view = "abc"_view;
    std::string result;
    for (auto byte : view) {
        result.push_back(static_cast<char>(byte));
    }
    EXPECT_EQ(result, "abc");
}

TEST(DataView, ToNumberLE) {
    uint8_t data[] = {0x01, 0x00, 0x00, 0x00};
    DataView view(data, 4);
    auto num = view.toNumberLE<uint32_t>();
    ASSERT_TRUE(num.has_value());
    EXPECT_EQ(*num, 1u);
}

TEST(DataView, ToNumberBE) {
    uint8_t data[] = {0x00, 0x00, 0x00, 0x01};
    DataView view(data, 4);
    auto num = view.toNumberBE<uint32_t>();
    ASSERT_TRUE(num.has_value());
    EXPECT_EQ(*num, 1u);
}

TEST(DataView, ToNumberTooSmall) {
    uint8_t data[] = {0x01, 0x02};
    DataView view(data, 2);
    auto num = view.toNumber<uint32_t>();
    EXPECT_FALSE(num.has_value());
}

TEST(DataView, ReadNumberLE) {
    uint8_t data[] = {0x78, 0x56, 0x34, 0x12};
    DataView view(data, 4);
    uint32_t value = 0;
    EXPECT_TRUE(view.readNumberLE(value));
    EXPECT_EQ(value, 0x12345678u);
}

TEST(DataView, ReadNumberBE) {
    uint8_t data[] = {0x12, 0x34, 0x56, 0x78};
    DataView view(data, 4);
    uint32_t value = 0;
    EXPECT_TRUE(view.readNumberBE(value));
    EXPECT_EQ(value, 0x12345678u);
}

TEST(DataView, ReadNumberPartialBytes) {
    uint8_t data[] = {0xAB, 0xCD, 0xEF};
    DataView view(data, 3);
    uint32_t value = 0;
    EXPECT_TRUE(view.readNumberBE(value, 3));
    EXPECT_EQ(value, 0xABCDEFu);
}

TEST(DataView, ReadNumberZeroBytes) {
    uint8_t data[] = {0x01};
    DataView view(data, 1);
    uint32_t value = 0;
    EXPECT_FALSE(view.readNumberBE(value, 0));
}

TEST(DataView, ReadNumberTooManyBytes) {
    uint8_t data[] = {0x01};
    DataView view(data, 1);
    uint32_t value = 0;
    EXPECT_FALSE(view.readNumberBE(value, 4));
}

TEST(DataView, Hash) {
    auto a = "hello"_view;
    auto b = "hello"_view;
    auto c = "world"_view;

    std::hash<DataView> hasher;
    EXPECT_EQ(hasher(a), hasher(b));
    EXPECT_NE(hasher(a), hasher(c));
}

TEST(DataView, CopyAndMove) {
    auto original = "hello"_view;
    DataView copy = original;
    EXPECT_EQ(copy, original);

    DataView moved = std::move(copy);
    EXPECT_EQ(moved, original);
}

TEST(DataView, EmptyOperations) {
    DataView view;
    EXPECT_TRUE(view.startsWith(DataView{}));
    EXPECT_TRUE(view.endsWith(DataView{}));
    EXPECT_TRUE(view.contains(DataView{}));
    EXPECT_EQ(view.find('a'), DataView::npos);
}
