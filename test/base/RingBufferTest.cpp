//
// Created by Nevermore on 2025/4/21.
// test TestLockfreeRingBuffer
// Copyright (c) 2025 Nevermore All rights reserved.
//
#include <gtest/gtest.h>
#include <thread>
#include <vector>
#include "RingBuffer.hpp"

using namespace slark;

class RingBufferTest : public ::testing::Test {
protected:
    static constexpr uint32_t TEST_CAPACITY = 16;
    RingBuffer<int, TEST_CAPACITY> buffer;
};

// 基础功能测试
TEST_F(RingBufferTest, InitialState) {
    EXPECT_TRUE(buffer.isEmpty());
    EXPECT_FALSE(buffer.isFull());
    EXPECT_EQ(buffer.length(), 0);
    EXPECT_EQ(buffer.capacity(), TEST_CAPACITY);
    EXPECT_EQ(buffer.tail(), TEST_CAPACITY);
}

TEST_F(RingBufferTest, WriteAndRead) {
    int data[] = {1, 2, 3, 4, 5};
    EXPECT_EQ(buffer.write(data, 5), 5);
    EXPECT_EQ(buffer.length(), 5);

    int readData[5] = {0};
    EXPECT_EQ(buffer.read(readData, 5), 5);
    for(int i = 0; i < 5; ++i) {
        EXPECT_EQ(readData[i], data[i]);
    }
}

TEST_F(RingBufferTest, WriteExact) {
    int data[] = {1, 2, 3};
    EXPECT_TRUE(buffer.writeExact(data, 3));
    EXPECT_EQ(buffer.length(), 3);

    // 尝试写入超过剩余空间的数据
    EXPECT_FALSE(buffer.writeExact(data, TEST_CAPACITY));
}

TEST_F(RingBufferTest, ReadExact) {
    int data[] = {1, 2, 3};
    buffer.write(data, 3);

    int readData[3] = {0};
    EXPECT_TRUE(buffer.readExact(readData, 3));
    for(int i = 0; i < 3; ++i) {
        EXPECT_EQ(readData[i], data[i]);
    }

    // 尝试读取超过可用数据的数量
    EXPECT_FALSE(buffer.readExact(readData, 1));
}

TEST_F(RingBufferTest, WrapAround) {
    std::vector<int> data(TEST_CAPACITY - 2);
    for(int i = 0; i < TEST_CAPACITY - 2; ++i) {
        data[i] = i;
    }

    // 填充缓冲区
    EXPECT_EQ(buffer.write(data.data(), data.size()), data.size());

    // 读取部分数据
    std::vector<int> readData(TEST_CAPACITY/2);
    EXPECT_EQ(buffer.read(readData.data(), TEST_CAPACITY/2), TEST_CAPACITY/2);

    // 写入新数据，触发环绕
    int newData[] = {100, 101, 102};
    EXPECT_EQ(buffer.write(newData, 3), 3);

    // 验证所有数据
    auto remainSize = buffer.length();
    std::vector<int> allData(remainSize);
    EXPECT_EQ(buffer.read(allData.data(), remainSize), remainSize);
    EXPECT_TRUE(buffer.isEmpty());
}

TEST_F(RingBufferTest, EdgeCases) {
    // 空指针测试
    EXPECT_EQ(buffer.write(nullptr, 5), 0);
    EXPECT_EQ(buffer.read(nullptr, 5), 0);
    EXPECT_FALSE(buffer.writeExact(nullptr, 5));
    EXPECT_FALSE(buffer.readExact(nullptr, 5));

    // 零长度测试
    int data[1];
    EXPECT_EQ(buffer.write(data, 0), 0);
    EXPECT_EQ(buffer.read(data, 0), 0);
    EXPECT_TRUE(buffer.writeExact(data, 0));
    EXPECT_TRUE(buffer.readExact(data, 0));
}

TEST_F(RingBufferTest, Reset) {
    int data[] = {1, 2, 3};
    buffer.write(data, 3);

    buffer.reset();
    EXPECT_TRUE(buffer.isEmpty());
    EXPECT_EQ(buffer.length(), 0);
    EXPECT_EQ(buffer.tail(), TEST_CAPACITY);

    // 确保重置后可以正常写入
    EXPECT_EQ(buffer.write(data, 3), 3);
}

TEST_F(RingBufferTest, FullBuffer) {
    std::vector<int> data(TEST_CAPACITY);
    for(int i = 0; i < TEST_CAPACITY; ++i) {
        data[i] = i;
    }

    EXPECT_EQ(buffer.write(data.data(), TEST_CAPACITY), TEST_CAPACITY);
    EXPECT_TRUE(buffer.isFull());
    EXPECT_EQ(buffer.tail(), 0);

    // 尝试继续写入
    int extraData = 100;
    EXPECT_EQ(buffer.write(&extraData, 1), 0);
}


class LockFreeRingBufferTest : public ::testing::Test {
protected:
    static constexpr uint32_t TEST_CAPACITY = 1024;
    SPSCRingBuffer<int, TEST_CAPACITY> buffer;
};

TEST_F(LockFreeRingBufferTest, InitialState) {
    EXPECT_TRUE(buffer.isEmpty());
    EXPECT_FALSE(buffer.isFull());
    EXPECT_EQ(buffer.length(), 0);
    EXPECT_EQ(buffer.capacity(), TEST_CAPACITY);
    EXPECT_EQ(buffer.tail(), TEST_CAPACITY);
}

TEST_F(LockFreeRingBufferTest, WriteAndRead) {
    int data[] = {1, 2, 3, 4, 5};
    EXPECT_EQ(buffer.write(data, 5), 5);
    EXPECT_EQ(buffer.length(), 5);

    int readData[5] = {0};
    EXPECT_EQ(buffer.read(readData, 5), 5);
    for(int i = 0; i < 5; ++i) {
        EXPECT_EQ(readData[i], data[i]);
    }
}

TEST_F(LockFreeRingBufferTest, WriteExact) {
    int data[] = {1, 2, 3};
    EXPECT_TRUE(buffer.writeExact(data, 3));
    EXPECT_EQ(buffer.length(), 3);

    // 尝试写入超过剩余空间的数据
    EXPECT_FALSE(buffer.writeExact(data, TEST_CAPACITY));
}

TEST_F(LockFreeRingBufferTest, ReadExact) {
    int data[] = {1, 2, 3};
    buffer.write(data, 3);

    int readData[3] = {0};
    EXPECT_TRUE(buffer.readExact(readData, 3));
    for(int i = 0; i < 3; ++i) {
        EXPECT_EQ(readData[i], data[i]);
    }

    // 尝试读取超过可用数据的数量
    EXPECT_FALSE(buffer.readExact(readData, 1));
}

TEST_F(LockFreeRingBufferTest, WrapAround) {
    std::vector<int> data(TEST_CAPACITY - 2);
    for(int i = 0; i < TEST_CAPACITY - 2; ++i) {
        data[i] = i;
    }

    // 填充缓冲区
    EXPECT_EQ(buffer.write(data.data(), data.size()), data.size());

    // 读取部分数据
    std::vector<int> readData(TEST_CAPACITY/2);
    EXPECT_EQ(buffer.read(readData.data(), TEST_CAPACITY/2), TEST_CAPACITY/2);

    // 写入新数据，触发环绕
    int newData[] = {100, 101, 102};
    EXPECT_EQ(buffer.write(newData, 3), 3);

    // 验证所有数据
    auto remainSize = buffer.length();
    std::vector<int> allData(remainSize);
    EXPECT_EQ(buffer.read(allData.data(), remainSize), remainSize);
    EXPECT_TRUE(buffer.isEmpty());
}

TEST_F(LockFreeRingBufferTest, EdgeCases) {
    // 空指针测试
    EXPECT_EQ(buffer.write(nullptr, 5), 0);
    EXPECT_EQ(buffer.read(nullptr, 5), 0);
    EXPECT_FALSE(buffer.writeExact(nullptr, 5));
    EXPECT_FALSE(buffer.readExact(nullptr, 5));

    // 零长度测试
    int data[1];
    EXPECT_EQ(buffer.write(data, 0), 0);
    EXPECT_EQ(buffer.read(data, 0), 0);
    EXPECT_TRUE(buffer.writeExact(data, 0));
    EXPECT_TRUE(buffer.readExact(data, 0));
}

TEST_F(LockFreeRingBufferTest, Reset) {
    int data[] = {1, 2, 3};
    buffer.write(data, 3);

    buffer.reset();
    EXPECT_TRUE(buffer.isEmpty());
    EXPECT_EQ(buffer.length(), 0);
    EXPECT_EQ(buffer.tail(), TEST_CAPACITY);

    // 确保重置后可以正常写入
    EXPECT_EQ(buffer.write(data, 3), 3);
}

TEST_F(LockFreeRingBufferTest, FullBuffer) {
    std::vector<int> data(TEST_CAPACITY);
    for(int i = 0; i < TEST_CAPACITY; ++i) {
        data[i] = i;
    }

    EXPECT_EQ(buffer.write(data.data(), TEST_CAPACITY), TEST_CAPACITY);
    EXPECT_TRUE(buffer.isFull());
    EXPECT_EQ(buffer.tail(), 0);

    // 尝试继续写入
    int extraData = 100;
    EXPECT_EQ(buffer.write(&extraData, 1), 0);
}

// 并发测试
TEST_F(LockFreeRingBufferTest, ConcurrentWriteRead) {
    static constexpr int NUM_ITEMS = 10000;
    std::atomic<bool> startFlag{false};
    std::atomic<int> readSum{0};
    std::atomic<int> writeSum{0};

    // 生产者线程
    std::thread producer([&]() {
        while(!startFlag.load()) {} // 等待开始信号

        for(int i = 1; i <= NUM_ITEMS; ++i) {
            while(!buffer.writeExact(&i, 1)) {
                std::this_thread::yield();
            }
            writeSum += i;
        }
    });

    // 消费者线程
    std::thread consumer([&]() {
        while(!startFlag.load()) {} // 等待开始信号

        int count = 0;
        while(count < NUM_ITEMS) {
            int value;
            if(buffer.readExact(&value, 1)) {
                readSum += value;
                ++count;
            } else {
                std::this_thread::yield();
            }
        }
    });

    startFlag.store(true); // 开始测试
    producer.join();
    consumer.join();

    EXPECT_EQ(writeSum.load(), readSum.load());
}

TEST_F(LockFreeRingBufferTest, SPSCBatchReadWrite) {
    static constexpr int BATCH_SIZE = 4;
    static constexpr int NUM_BATCHES = 1000;

    std::atomic<bool> startFlag{false};
    std::atomic<int> readCount{0};
    std::atomic<int> writeCount{0};

    // 生产者线程
    std::thread producer([&]() {
        while(!startFlag.load()) {}

        std::vector<int> data(BATCH_SIZE);
        for(int batch = 0; batch < NUM_BATCHES; ++batch) {
            for(int i = 0; i < BATCH_SIZE; ++i) {
                data[i] = batch * BATCH_SIZE + i + 1;
            }
            while(!buffer.writeExact(data.data(), BATCH_SIZE)) {
                std::this_thread::yield();
            }
            writeCount += BATCH_SIZE;
        }
    });

    // 消费者线程
    std::thread consumer([&]() {
        while(!startFlag.load()) {}

        std::vector<int> data(BATCH_SIZE);
        while(readCount < NUM_BATCHES * BATCH_SIZE) {
            if(buffer.readExact(data.data(), BATCH_SIZE)) {
                readCount += BATCH_SIZE;
            } else {
                std::this_thread::yield();
            }
        }
    });

    startFlag.store(true);
    producer.join();
    consumer.join();

    EXPECT_EQ(writeCount.load(), readCount.load());
    EXPECT_EQ(writeCount.load(), NUM_BATCHES * BATCH_SIZE);
}

TEST_F(LockFreeRingBufferTest, StressTest) {
    static constexpr int NUM_ITERATIONS = 100000;
    std::atomic<bool> startFlag{false};
    std::atomic<bool> stopFlag{false};
    std::atomic<uint64_t> totalWritten{0};
    std::atomic<uint64_t> totalRead{0};

    // 生产者线程
    std::thread producer([&]() {
        while(!startFlag.load()) {}

        for(int i = 1; i <= NUM_ITERATIONS && !stopFlag.load(); ++i) {
            if(buffer.writeExact(&i, 1)) {
                totalWritten++;
            }
            std::this_thread::yield();
        }
    });

    // 消费者线程
    std::thread consumer([&]() {
        while(!startFlag.load()) {}

        int value;
        while(!stopFlag.load() || !buffer.isEmpty()) {
            if(buffer.readExact(&value, 1)) {
                totalRead++;
            }
            std::this_thread::yield();
        }
    });

    startFlag.store(true);
    std::this_thread::sleep_for(std::chrono::seconds(2));
    stopFlag.store(true);

    producer.join();
    consumer.join();

    EXPECT_EQ(totalWritten.load(), totalRead.load());
}
