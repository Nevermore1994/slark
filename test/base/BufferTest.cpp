//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#include "Buffer.hpp"

#include <gtest/gtest.h>
#include <string_view>

using namespace slark;

TEST(BufferTest, CheckMinSize) {
    Buffer buffer;
    EXPECT_TRUE(buffer.empty());
    EXPECT_FALSE(buffer.require(10));
    auto data = std::make_unique<Data>("hello world!");
    buffer.append(0, std::move(data));
    EXPECT_TRUE(buffer.require(10));
    EXPECT_FALSE(buffer.require(20));
    EXPECT_TRUE(!buffer.empty());
}

TEST(BufferTest, Append) {
    Buffer buffer;
    EXPECT_TRUE(buffer.empty());
    auto data = std::make_unique<Data>("hello world!");
    buffer.append(0, std::move(data));
    buffer.append(0, std::make_unique<Data>(5050, [](uint8_t* data) {
                      std::fill_n(data, 5050, 'a');
                      return 5050;
                  }));
    EXPECT_TRUE(buffer.require(5062));
    EXPECT_TRUE(buffer.length() == 5062);
}

TEST(BufferTest, ReadByte) {
    Buffer buffer;
    auto data = std::make_unique<Data>("abcd");
    buffer.append(0, std::move(data));
    uint32_t t = 0;
    buffer.read4ByteLE(t);
    EXPECT_EQ(buffer.readPos(), 4);
    EXPECT_EQ(1684234849, t);
    EXPECT_TRUE(buffer.empty());
    buffer.shrink();
    EXPECT_EQ(buffer.length(), 0);
    EXPECT_EQ(buffer.readPos(), 0);
    EXPECT_EQ(buffer.pos(), 4);

    data = std::make_unique<Data>("abcd");
    buffer.append(4, std::move(data));
    buffer.read4ByteBE(t);
    EXPECT_EQ(buffer.readPos(), 4);
    EXPECT_EQ(1633837924, t);
    EXPECT_TRUE(buffer.empty());
    EXPECT_EQ(buffer.pos(), 8);
    buffer.shrink();
    EXPECT_EQ(buffer.length(), 0);
    EXPECT_EQ(buffer.readPos(), 0);
    EXPECT_EQ(buffer.pos(), 8);
}

TEST(BufferTest, ReadData) {
    Buffer buffer(200);
    auto data = std::make_unique<Data>("abcd");
    buffer.append(0, std::move(data));
    auto ptr = buffer.readData(buffer.length());
    EXPECT_EQ(ptr->toStringView(), std::string_view("abcd"));
    EXPECT_TRUE(buffer.empty());
    EXPECT_EQ(buffer.pos(), 4);

    data = std::make_unique<Data>("hello world!");
    buffer.append(0, std::move(data));
    ptr = buffer.readData(buffer.length());
    EXPECT_EQ(ptr->toStringView(), std::string_view("hello world!"));
    EXPECT_EQ(buffer.pos(), 12 + 4);
}

TEST(BufferTest, ReadString) {
    Buffer buffer;
    auto data = std::make_unique<Data>("hello world");
    buffer.append(0, std::move(data));

    std::string str;
    EXPECT_TRUE(buffer.readString(5, str));
    EXPECT_EQ(str, "hello");

    EXPECT_TRUE(buffer.readString(6, str));
    EXPECT_EQ(str, " world");

    EXPECT_FALSE(buffer.readString(100, str));
}

TEST(BufferTest, ReadLine) {
    Buffer buffer;
    auto data = std::make_unique<Data>("line1\nline2\nline3");
    buffer.append(0, std::move(data));

    DataView line;
    EXPECT_TRUE(buffer.readLine(line));
    EXPECT_EQ(line.toStringView(), "line1");

    EXPECT_TRUE(buffer.readLine(line));
    EXPECT_EQ(line.toStringView(), "line2");

    EXPECT_FALSE(buffer.readLine(line));
}

TEST(BufferTest, BitReadView) {
    Buffer buffer;
    auto data = std::make_unique<Data>(std::string_view("\xAB\xCD"));
    buffer.append(0, std::move(data));

    BitReadView bitView(buffer);

    // \xAB = 10101011, first bit is 1
    EXPECT_EQ(bitView.readBit(), 1u);
    // next 7 bits: 0101011 = 0x2B = 43
    EXPECT_EQ(bitView.readBits(7), 43u);
    // \xCD = 11001101
    EXPECT_EQ(bitView.readBits(8), 0xCDu);
}

TEST(BufferTest, DataViewIntegration) {
    Buffer buffer;
    auto data = std::make_unique<Data>("hello world");
    buffer.append(0, std::move(data));

    DataView view = buffer.view();
    EXPECT_EQ(view.size(), 11u);
    EXPECT_EQ(view.toStringView(), "hello world");

    DataView shot = buffer.shotView();
    EXPECT_EQ(shot.toStringView(), "hello world");

    buffer.skip(6);
    shot = buffer.shotView();
    EXPECT_EQ(shot.toStringView(), "world");
}
