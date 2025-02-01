//
// Created by Nevermore on 2024/9/29.
// slark BufferTest
// Copyright (c) 2024 Nevermore All rights reserved.
//

#include <algorithm>
#include <gtest/gtest.h>
#include <string_view>
#include "Buffer.hpp"
#include "Util.hpp"

using namespace slark;

// Test checkMinSize
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

// Test append
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

// Test read4ByteLE
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
    buffer.append(0,std::move(data));
    auto ptr = buffer.readData(buffer.length());
    EXPECT_EQ(ptr->view().view(), std::string_view("abcd"));
    EXPECT_TRUE(buffer.empty());
    EXPECT_EQ(buffer.pos(), 4);

    data = std::make_unique<Data>("hello world!");
    buffer.append(0, std::move(data));
    ptr = buffer.readData(buffer.length());
    EXPECT_EQ(ptr->view().view(), std::string_view("hello world!"));
    EXPECT_EQ(buffer.pos(), 12 + 4);
}
