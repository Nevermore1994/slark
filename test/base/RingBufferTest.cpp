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

    EXPECT_FALSE(buffer.readExact(readData, 1));
}

TEST_F(RingBufferTest, WrapAround) {
    std::vector<int> data(TEST_CAPACITY - 2);
    for(int i = 0; i < TEST_CAPACITY - 2; ++i) {
        data[i] = i;
    }

    EXPECT_EQ(buffer.write(data.data(), data.size()), data.size());


    std::vector<int> readData(TEST_CAPACITY/2);
    EXPECT_EQ(buffer.read(readData.data(), TEST_CAPACITY/2), TEST_CAPACITY/2);

    int newData[] = {100, 101, 102};
    EXPECT_EQ(buffer.write(newData, 3), 3);

    auto remainSize = buffer.length();
    std::vector<int> allData(remainSize);
    EXPECT_EQ(buffer.read(allData.data(), remainSize), remainSize);
    EXPECT_TRUE(buffer.isEmpty());
}

TEST_F(RingBufferTest, EdgeCases) {
    EXPECT_EQ(buffer.write(nullptr, 5), 0);
    EXPECT_EQ(buffer.read(nullptr, 5), 0);
    EXPECT_FALSE(buffer.writeExact(nullptr, 5));
    EXPECT_FALSE(buffer.readExact(nullptr, 5));

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

    int extraData = 100;
    EXPECT_EQ(buffer.write(&extraData, 1), 0);
}


class SyncRingBufferTest : public ::testing::Test {
protected:
    static constexpr uint32_t TEST_CAPACITY = 1024;
    SyncRingBuffer<int, TEST_CAPACITY> buffer;
};

TEST_F(SyncRingBufferTest, InitialState) {
    EXPECT_TRUE(buffer.isEmpty());
    EXPECT_FALSE(buffer.isFull());
    EXPECT_EQ(buffer.length(), 0);
    EXPECT_EQ(buffer.capacity(), TEST_CAPACITY);
    EXPECT_EQ(buffer.tail(), TEST_CAPACITY);
}

TEST_F(SyncRingBufferTest, WriteAndRead) {
    int data[] = {1, 2, 3, 4, 5};
    EXPECT_EQ(buffer.write(data, 5), 5);
    EXPECT_EQ(buffer.length(), 5);

    int readData[5] = {0};
    EXPECT_EQ(buffer.read(readData, 5), 5);
    for(int i = 0; i < 5; ++i) {
        EXPECT_EQ(readData[i], data[i]);
    }
}

TEST_F(SyncRingBufferTest, WriteExact) {
    int data[] = {1, 2, 3};
    EXPECT_TRUE(buffer.writeExact(data, 3));
    EXPECT_EQ(buffer.length(), 3);
    
    EXPECT_FALSE(buffer.writeExact(data, TEST_CAPACITY));
}

TEST_F(SyncRingBufferTest, ReadExact) {
    int data[] = {1, 2, 3};
    buffer.write(data, 3);

    int readData[3] = {0};
    EXPECT_TRUE(buffer.readExact(readData, 3));
    for(int i = 0; i < 3; ++i) {
        EXPECT_EQ(readData[i], data[i]);
    }
    EXPECT_FALSE(buffer.readExact(readData, 1));
}

TEST_F(SyncRingBufferTest, WrapAround) {
    std::vector<int> data(TEST_CAPACITY - 2);
    for(int i = 0; i < TEST_CAPACITY - 2; ++i) {
        data[i] = i;
    }
    
    EXPECT_EQ(buffer.write(data.data(), data.size()), data.size());
    
    std::vector<int> readData(TEST_CAPACITY/2);
    EXPECT_EQ(buffer.read(readData.data(), TEST_CAPACITY/2), TEST_CAPACITY/2);

    int newData[] = {100, 101, 102};
    EXPECT_EQ(buffer.write(newData, 3), 3);
    
    auto remainSize = buffer.length();
    std::vector<int> allData(remainSize);
    EXPECT_EQ(buffer.read(allData.data(), remainSize), remainSize);
    EXPECT_TRUE(buffer.isEmpty());
}

TEST_F(SyncRingBufferTest, EdgeCases) {
    EXPECT_EQ(buffer.write(nullptr, 5), 0);
    EXPECT_EQ(buffer.read(nullptr, 5), 0);
    EXPECT_FALSE(buffer.writeExact(nullptr, 5));
    EXPECT_FALSE(buffer.readExact(nullptr, 5));
    
    int data[1];
    EXPECT_EQ(buffer.write(data, 0), 0);
    EXPECT_EQ(buffer.read(data, 0), 0);
    EXPECT_TRUE(buffer.writeExact(data, 0));
    EXPECT_TRUE(buffer.readExact(data, 0));
}

TEST_F(SyncRingBufferTest, Reset) {
    int data[] = {1, 2, 3};
    buffer.write(data, 3);

    buffer.reset();
    EXPECT_TRUE(buffer.isEmpty());
    EXPECT_EQ(buffer.length(), 0);
    EXPECT_EQ(buffer.tail(), TEST_CAPACITY);
    
    EXPECT_EQ(buffer.write(data, 3), 3);
}

TEST_F(SyncRingBufferTest, FullBuffer) {
    std::vector<int> data(TEST_CAPACITY);
    for(int i = 0; i < TEST_CAPACITY; ++i) {
        data[i] = i;
    }

    EXPECT_EQ(buffer.write(data.data(), TEST_CAPACITY), TEST_CAPACITY);
    EXPECT_TRUE(buffer.isFull());
    EXPECT_EQ(buffer.tail(), 0);
    
    int extraData = 100;
    EXPECT_EQ(buffer.write(&extraData, 1), 0);
}


TEST_F(SyncRingBufferTest, ConcurrentWriteRead) {
    static constexpr int NUM_ITEMS = 10000;
    std::atomic<bool> startFlag{false};
    std::atomic<int> readSum{0};
    std::atomic<int> writeSum{0};
    
    std::thread producer([&]() {
        while(!startFlag.load()) {} 

        for(int i = 1; i <= NUM_ITEMS; ++i) {
            while(!buffer.writeExact(&i, 1)) {
                std::this_thread::yield();
            }
            writeSum += i;
        }
    });
    
    std::thread consumer([&]() {
        while(!startFlag.load()) {}

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

    startFlag.store(true); 
    producer.join();
    consumer.join();

    EXPECT_EQ(writeSum.load(), readSum.load());
}

TEST_F(SyncRingBufferTest, SPSCBatchReadWrite) {
    static constexpr int BATCH_SIZE = 4;
    static constexpr int NUM_BATCHES = 1000;

    std::atomic<bool> startFlag{false};
    std::atomic<int> readCount{0};
    std::atomic<int> writeCount{0};
    
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

TEST_F(SyncRingBufferTest, StressTest) {
    static constexpr int NUM_ITERATIONS = 100000;
    std::atomic<bool> startFlag{false};
    std::atomic<bool> stopFlag{false};
    std::atomic<uint64_t> totalWritten{0};
    std::atomic<uint64_t> totalRead{0};
    
    std::thread producer([&]() {
        while(!startFlag.load()) {}

        for(int i = 1; i <= NUM_ITERATIONS && !stopFlag.load(); ++i) {
            if(buffer.writeExact(&i, 1)) {
                totalWritten++;
            }
            std::this_thread::yield();
        }
    });
    
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


TEST_F(SyncRingBufferTest, MultiThreadedReadWrite) {
    static constexpr int NUM_PRODUCERS = 4;
    static constexpr int NUM_CONSUMERS = 4;
    static constexpr int ITEMS_PER_PRODUCER = 1000;

    std::atomic<bool> startFlag{false};
    std::atomic<int> totalWritten{0};
    std::atomic<int> totalRead{0};
    std::vector<std::thread> producers;
    std::vector<std::thread> consumers;
    
    for (int p = 0; p < NUM_PRODUCERS; ++p) {
        producers.emplace_back([&, p]() {
            while (!startFlag.load()) {}

            for (int i = 0; i < ITEMS_PER_PRODUCER; ++i) {
                int value = p * ITEMS_PER_PRODUCER + i + 1;
                while (!buffer.writeExact(&value, 1)) {
                    std::this_thread::yield();
                }
                totalWritten.fetch_add(1);
            }
        });
    }
    
    for (int c = 0; c < NUM_CONSUMERS; ++c) {
        consumers.emplace_back([&]() {
            while (!startFlag.load()) {}

            while (totalRead < NUM_PRODUCERS * ITEMS_PER_PRODUCER) {
                int value;
                if (buffer.readExact(&value, 1)) {
                    totalRead.fetch_add(1);
                } else {
                    std::this_thread::yield();
                }
            }
        });
    }

    startFlag.store(true);

    for (auto& p : producers) {
        p.join();
    }
    for (auto& c : consumers) {
        c.join();
    }

    EXPECT_EQ(totalWritten.load(), NUM_PRODUCERS * ITEMS_PER_PRODUCER);
    EXPECT_EQ(totalRead.load(), NUM_PRODUCERS * ITEMS_PER_PRODUCER);
    EXPECT_TRUE(buffer.isEmpty());
}

TEST_F(SyncRingBufferTest, RestTest) {
    static constexpr int NUM_ITERATIONS = 100000;
    std::atomic<bool> startFlag{false};
    std::atomic<bool> stopFlag{false};
    std::atomic<uint64_t> totalWritten{0};
    std::atomic<uint64_t> totalRead{0};
    std::atomic<uint64_t> totalDiscard{0};

    std::thread producer([&]() {
        while(!startFlag.load()) {}

        for(int i = 1; i <= NUM_ITERATIONS && !stopFlag.load(); ++i) {
            if(buffer.writeExact(&i, 1)) {
                totalWritten++;
            }
            if (i % 20 == 0) {
                auto discard = buffer.reset();
                totalDiscard.fetch_add(static_cast<uint64_t>(discard));
            }
            std::this_thread::yield();
        }
    });

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

    EXPECT_EQ(totalWritten.load(), totalRead.load() + totalDiscard.load());
}
