//
// Created by Nevermore on 2024/4/12.
// slark RingBufferTest
// Copyright (c) 2024 Nevermore All rights reserved.
//
#include <gtest/gtest.h>
#include <chrono>
#include "RingBuffer.hpp"

using namespace slark;

// Basic read/write test
TEST(RingBuffer, Basic) {
    RingBuffer<int, 10> buffer;
    int data[] = {1, 2, 3, 4, 5};
    ASSERT_EQ(buffer.write(data, 5), 5);
    ASSERT_EQ(buffer.length(), 5);

    int readData[5] = {0};
    ASSERT_EQ(buffer.read(readData, 5), 5);
    ASSERT_EQ(std::memcmp(data, readData, 5 * sizeof(int)), 0);
    ASSERT_TRUE(buffer.isEmpty());
}

// Exact read/write test
TEST(RingBuffer, ExactReadWrite) {
    RingBuffer<int, 5> buffer;
    int data[] = {1, 2, 3, 4, 5};
    
    // Test exact write
    ASSERT_FALSE(buffer.writeExact(data, 6));  // Exceeds capacity
    ASSERT_TRUE(buffer.writeExact(data, 5));   // Exactly full
    ASSERT_TRUE(buffer.isFull());

    // Test exact read
    int readData[5] = {0};
    ASSERT_FALSE(buffer.readExact(readData, 6));  // Request too much
    ASSERT_TRUE(buffer.readExact(readData, 5));   // Read exactly
    ASSERT_TRUE(buffer.isEmpty());
}

// Circular write test
TEST(RingBuffer, CircularWrite) {
    RingBuffer<int, 5> buffer;
    int data1[] = {1, 2, 3};
    int data2[] = {4, 5};
    int data3[] = {6, 7};
    
    ASSERT_EQ(buffer.write(data1, 3), 3);
    ASSERT_EQ(buffer.read(data1, 2), 2);  // Read first two elements
    ASSERT_EQ(buffer.write(data2, 2), 2); // Write new data
    ASSERT_EQ(buffer.length(), 3);

    int readData[5] = {0};
    ASSERT_EQ(buffer.read(readData, 3), 3);
    ASSERT_EQ(readData[0], 3);
    ASSERT_EQ(readData[1], 4);
    ASSERT_EQ(readData[2], 5);
}

// Edge cases test
TEST(RingBuffer, EdgeCases) {
    RingBuffer<int, 3> buffer;
    
    // Empty buffer test
    ASSERT_TRUE(buffer.isEmpty());
    ASSERT_EQ(buffer.length(), 0);
    int data = 0;
    ASSERT_EQ(buffer.read(&data, 1), 0);

    // Full buffer test
    int writeData[] = {1, 2, 3};
    ASSERT_EQ(buffer.write(writeData, 3), 3);
    ASSERT_TRUE(buffer.isFull());
    ASSERT_EQ(buffer.write(&data, 1), 0);

    // Reset test
    buffer.reset();
    ASSERT_TRUE(buffer.isEmpty());
    ASSERT_EQ(buffer.length(), 0);
}

// Partial read/write test
TEST(RingBuffer, PartialReadWrite) {
    RingBuffer<int, 5> buffer;
    int writeData[] = {1, 2, 3, 4, 5, 6};
    
    // Test partial write
    ASSERT_EQ(buffer.write(writeData, 6), 5); // Can only write 5 elements
    ASSERT_TRUE(buffer.isFull());

    // Test partial read
    int readData[3] = {0};
    ASSERT_EQ(buffer.read(readData, 3), 3);
    ASSERT_EQ(buffer.length(), 2);
    
    // Verify partial read data
    ASSERT_EQ(readData[0], 1);
    ASSERT_EQ(readData[1], 2);
    ASSERT_EQ(readData[2], 3);
}

// Zero length operations test
TEST(RingBuffer, ZeroLength) {
    RingBuffer<int, 5> buffer;
    int data[] = {1, 2, 3};
    
    ASSERT_EQ(buffer.write(nullptr, 0), 0);
    ASSERT_TRUE(buffer.writeExact(data, 0));
    
    ASSERT_EQ(buffer.read(nullptr, 0), 0);
    ASSERT_TRUE(buffer.readExact(data, 0));
}

// Long string test
TEST(RingBuffer, LongStr) {
    RingBuffer<char, 1024> buffer;
    std::string t = "Lorem ipsum dolor sit amet. Vel sapiente voluptates et magni veritatis aut velit deleniti? Ex beatae illo ut sint aperiam non adipisci similique est beatae voluptate id minus omnis est nemo eligendi. Eum omnis itaque sed dolores quae qui reiciendis excepturi! Et repellendus error sed iusto quod est accusamus blanditiis quo tempora cupiditate! Non voluptate nemo qui voluptates voluptatem qui dolorum reprehenderit. Sit aliquid distinctio ut vero dolores ea dolorum quia. Et dolor facilis quo possimus ipsa ut velit laudantium a deserunt placeat ab voluptates accusamus sed repudiandae similique. Qui quia rerum ad molestiae incidunt qui veniam asperiores.";
    
    // Test write long string
    ASSERT_EQ(buffer.write(t.c_str(), t.length()), t.length());
    ASSERT_EQ(buffer.length(), t.length());

    // Test read long string
    std::string readStr(t.length(), '\0');
    ASSERT_EQ(buffer.read(&readStr[0], t.length()), t.length());
    ASSERT_EQ(readStr, t);
    ASSERT_TRUE(buffer.isEmpty());
}

// Long string circular write test
TEST(RingBuffer, LongStrCircular) {
    RingBuffer<char, 1024> buffer;
    std::string longStr(2048, 'A');  // Create string larger than buffer size
    
    // Write in two parts and verify
    ASSERT_EQ(buffer.write(longStr.c_str(), 800), 800);
    ASSERT_EQ(buffer.length(), 800);
    
    // Read half
    std::string readStr(400, '\0');
    ASSERT_EQ(buffer.read(&readStr[0], 400), 400);
    ASSERT_EQ(readStr, std::string(400, 'A'));
    
    // Continue writing, test circular buffer
    ASSERT_EQ(buffer.write(longStr.c_str() + 800, 400), 400);
    ASSERT_EQ(buffer.length(), 800);
    
    // Verify integrity
    std::string finalStr(800, '\0');
    ASSERT_EQ(buffer.read(&finalStr[0], 800), 800);
    ASSERT_EQ(finalStr.substr(0, 400), std::string(400, 'A'));
    ASSERT_EQ(finalStr.substr(400), std::string(400, 'A'));
}