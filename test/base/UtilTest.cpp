//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//
#include "Util.h"

#include <array>
#include <gtest/gtest.h>
#include <set>
#include <string>

using namespace slark;

TEST(Util, GenRandomName) {
    auto name = Util::genRandomName("test_");
    EXPECT_TRUE(name.starts_with("test_"));
    EXPECT_EQ(name.size(), 5 + 8);
}

TEST(Util, GenRandomNameUniqueness) {
    std::set<std::string> names;
    for (int32_t i = 0; i < 100; ++i) {
        names.insert(Util::genRandomName("prefix_"));
    }
    EXPECT_EQ(names.size(), 100u);
}

TEST(Util, GenRandomNameEmptyPrefix) {
    auto name = Util::genRandomName("");
    EXPECT_EQ(name.size(), 8u);
}

TEST(Util, Uint32ToStringLE) {
    auto str = Util::toStringLE<uint32_t>(0x01020304);
    ASSERT_EQ(str.size(), 4u);
    EXPECT_EQ(static_cast<uint8_t>(str[0]), 0x04);
    EXPECT_EQ(static_cast<uint8_t>(str[1]), 0x03);
    EXPECT_EQ(static_cast<uint8_t>(str[2]), 0x02);
    EXPECT_EQ(static_cast<uint8_t>(str[3]), 0x01);
}

TEST(Util, Uint32ToStringBE) {
    auto str = Util::toStringBE<uint32_t>(0x01020304);
    ASSERT_EQ(str.size(), 4u);
    EXPECT_EQ(static_cast<uint8_t>(str[0]), 0x01);
    EXPECT_EQ(static_cast<uint8_t>(str[1]), 0x02);
    EXPECT_EQ(static_cast<uint8_t>(str[2]), 0x03);
    EXPECT_EQ(static_cast<uint8_t>(str[3]), 0x04);
}

TEST(Util, Uint32ToStringLEZero) {
    auto str = Util::toStringLE<uint32_t>(0);
    ASSERT_EQ(str.size(), 4u);
    for (auto c : str) {
        EXPECT_EQ(static_cast<uint8_t>(c), 0);
    }
}

TEST(Util, Uint32ToStringBEZero) {
    auto str = Util::toStringBE<uint32_t>(0);
    ASSERT_EQ(str.size(), 4u);
    for (auto c : str) {
        EXPECT_EQ(static_cast<uint8_t>(c), 0);
    }
}

TEST(Util, Uint16ToStringLE) {
    auto str = Util::toStringLE<uint16_t>(0x0102);
    ASSERT_EQ(str.size(), 2u);
    EXPECT_EQ(static_cast<uint8_t>(str[0]), 0x02);
    EXPECT_EQ(static_cast<uint8_t>(str[1]), 0x01);
}

TEST(Util, Uint16ToStringBE) {
    auto str = Util::toStringBE<uint16_t>(0x0102);
    ASSERT_EQ(str.size(), 2u);
    EXPECT_EQ(static_cast<uint8_t>(str[0]), 0x01);
    EXPECT_EQ(static_cast<uint8_t>(str[1]), 0x02);
}

TEST(Util, Uint64ToStringLE) {
    auto str = Util::toStringLE<uint64_t>(0x0102030405060708);
    ASSERT_EQ(str.size(), 8u);
    EXPECT_EQ(static_cast<uint8_t>(str[0]), 0x08);
    EXPECT_EQ(static_cast<uint8_t>(str[7]), 0x01);
}

TEST(Util, Uint64ToStringBE) {
    auto str = Util::toStringBE<uint64_t>(0x0102030405060708);
    ASSERT_EQ(str.size(), 8u);
    EXPECT_EQ(static_cast<uint8_t>(str[0]), 0x01);
    EXPECT_EQ(static_cast<uint8_t>(str[7]), 0x08);
}

TEST(Util, GetWeakPtrAddressValid) {
    auto shared = std::make_shared<int32_t>(42);
    std::weak_ptr<int32_t> weak = shared;
    auto result = Util::getWeakPtrAddress(weak);
    EXPECT_TRUE(result.has_value());
    EXPECT_NE(result.value(), 0u);
}

TEST(Util, GetWeakPtrAddressExpired) {
    std::weak_ptr<int32_t> weak;
    {
        auto shared = std::make_shared<int32_t>(42);
        weak = shared;
    }
    auto result = Util::getWeakPtrAddress(weak);
    EXPECT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), Error::NullPointer);
}

TEST(Util, GetWeakPtrAddressEmpty) {
    std::weak_ptr<int32_t> weak;
    auto result = Util::getWeakPtrAddress(weak);
    EXPECT_FALSE(result.has_value());
}

// --- Golomb Tests ---

TEST(Golomb, ReadBit) {
    // 0xA5 = 10100101
    uint8_t data[] = {0xA5};
    DataView view(data, 1);
    int32_t offset = 0;

    EXPECT_TRUE(Util::Golomb::readBit(view, offset));  // 1
    EXPECT_FALSE(Util::Golomb::readBit(view, offset)); // 0
    EXPECT_TRUE(Util::Golomb::readBit(view, offset));  // 1
    EXPECT_FALSE(Util::Golomb::readBit(view, offset)); // 0
    EXPECT_FALSE(Util::Golomb::readBit(view, offset)); // 0
    EXPECT_TRUE(Util::Golomb::readBit(view, offset));  // 1
    EXPECT_FALSE(Util::Golomb::readBit(view, offset)); // 0
    EXPECT_TRUE(Util::Golomb::readBit(view, offset));  // 1
    EXPECT_EQ(offset, 8);
}

TEST(Golomb, ReadBits) {
    // 0xFF = 11111111
    uint8_t data[] = {0xFF};
    DataView view(data, 1);
    int32_t offset = 0;

    auto val = Util::Golomb::readBits(view, offset, 4);
    EXPECT_EQ(val, 0x0Fu);
    EXPECT_EQ(offset, 4);

    val = Util::Golomb::readBits(view, offset, 4);
    EXPECT_EQ(val, 0x0Fu);
    EXPECT_EQ(offset, 8);
}

TEST(Golomb, ReadBitsMultiByte) {
    // 0xAB 0xCD = 10101011 11001101
    uint8_t data[] = {0xAB, 0xCD};
    DataView view(data, 2);
    int32_t offset = 0;

    auto val = Util::Golomb::readBits(view, offset, 16);
    EXPECT_EQ(val, 0xABCDu);
}

TEST(Golomb, ReadUeZero) {
    // Exp-Golomb code for 0: "1" -> bit pattern: 1xxxxxxx
    uint8_t data[] = {0x80}; // 10000000
    DataView view(data, 1);
    int32_t offset = 0;

    auto val = Util::Golomb::readUe(view, offset);
    EXPECT_EQ(val, 0u);
    EXPECT_EQ(offset, 1);
}

TEST(Golomb, ReadUeOne) {
    // Exp-Golomb code for 1: "010" -> bit pattern: 010xxxxx
    uint8_t data[] = {0x40}; // 01000000
    DataView view(data, 1);
    int32_t offset = 0;

    auto val = Util::Golomb::readUe(view, offset);
    EXPECT_EQ(val, 1u);
    EXPECT_EQ(offset, 3);
}

TEST(Golomb, ReadUeTwo) {
    // Exp-Golomb code for 2: "011" -> bit pattern: 011xxxxx
    uint8_t data[] = {0x60}; // 01100000
    DataView view(data, 1);
    int32_t offset = 0;

    auto val = Util::Golomb::readUe(view, offset);
    EXPECT_EQ(val, 2u);
    EXPECT_EQ(offset, 3);
}

TEST(Golomb, ReadUeThree) {
    // Exp-Golomb code for 3: "00100" -> bit pattern: 00100xxx
    uint8_t data[] = {0x20}; // 00100000
    DataView view(data, 1);
    int32_t offset = 0;

    auto val = Util::Golomb::readUe(view, offset);
    EXPECT_EQ(val, 3u);
    EXPECT_EQ(offset, 5);
}

TEST(Golomb, ReadUeSeven) {
    // Exp-Golomb code for 7: "00010 00" -> 3 leading zeros, then 1, then 000
    // = 0001000x -> 0x10
    uint8_t data[] = {0x10}; // 00010000
    DataView view(data, 1);
    int32_t offset = 0;

    auto val = Util::Golomb::readUe(view, offset);
    EXPECT_EQ(val, 7u);
    EXPECT_EQ(offset, 7);
}
