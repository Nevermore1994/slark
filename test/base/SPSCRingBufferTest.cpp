//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//
//

#include "SPSCRingBuffer.hpp"

#include <gtest/gtest.h>

#include <atomic>
#include <thread>
#include <vector>

namespace slark {

TEST(SPSCRingBufferTest, CapacityIsPowerOfTwo) {
    SPSCRingBuffer<int, 10> buf;
    EXPECT_EQ(buf.capacity(), 15u);
}

TEST(SPSCRingBufferTest, CapacityExactPowerOfTwo) {
    SPSCRingBuffer<int, 16> buf;
    EXPECT_EQ(buf.capacity(), 31u);
}

TEST(SPSCRingBufferTest, CapacityOne) {
    SPSCRingBuffer<int, 1> buf;
    EXPECT_EQ(buf.capacity(), 1u);
}

TEST(SPSCRingBufferTest, InitialState) {
    SPSCRingBuffer<int, 8> buf;
    EXPECT_TRUE(buf.isEmpty());
    EXPECT_FALSE(buf.isFull());
    EXPECT_EQ(buf.length(), 0u);
    EXPECT_EQ(buf.available(), buf.capacity());
}

TEST(SPSCRingBufferTest, WriteExactSuccess) {
    SPSCRingBuffer<int, 8> buf;
    int data[] = {1, 2, 3};
    EXPECT_TRUE(buf.writeExact(data, 3));
    EXPECT_EQ(buf.length(), 3u);
    EXPECT_FALSE(buf.isEmpty());
}

TEST(SPSCRingBufferTest, WriteExactInsufficientSpace) {
    SPSCRingBuffer<int, 4> buf;
    std::vector<int> data(7);
    EXPECT_TRUE(buf.writeExact(data.data(), 7));
    EXPECT_TRUE(buf.isFull());
    int extra = 1;
    EXPECT_FALSE(buf.writeExact(&extra, 1));
}

TEST(SPSCRingBufferTest, WriteExactZeroCount) {
    SPSCRingBuffer<int, 8> buf;
    int data[] = {1};
    EXPECT_TRUE(buf.writeExact(data, 0));
    EXPECT_TRUE(buf.isEmpty());
}

TEST(SPSCRingBufferTest, WriteExactNullPtr) {
    SPSCRingBuffer<int, 8> buf;
    EXPECT_FALSE(buf.writeExact(nullptr, 5));
}

TEST(SPSCRingBufferTest, ReadExactSuccess) {
    SPSCRingBuffer<int, 8> buf;
    int data[] = {10, 20, 30};
    buf.writeExact(data, 3);

    int readData[3] = {0};
    EXPECT_TRUE(buf.readExact(readData, 3));
    EXPECT_EQ(readData[0], 10);
    EXPECT_EQ(readData[1], 20);
    EXPECT_EQ(readData[2], 30);
    EXPECT_TRUE(buf.isEmpty());
}

TEST(SPSCRingBufferTest, ReadExactInsufficientData) {
    SPSCRingBuffer<int, 8> buf;
    int data[] = {1};
    buf.writeExact(data, 1);

    int readData[3] = {0};
    EXPECT_FALSE(buf.readExact(readData, 3));
    EXPECT_EQ(buf.length(), 1u);
}

TEST(SPSCRingBufferTest, ReadExactZeroCount) {
    SPSCRingBuffer<int, 8> buf;
    int data[] = {1};
    EXPECT_TRUE(buf.readExact(data, 0));
}

TEST(SPSCRingBufferTest, ReadExactNullPtr) {
    SPSCRingBuffer<int, 8> buf;
    int value = 1;
    buf.writeExact(&value, 1);
    EXPECT_FALSE(buf.readExact(nullptr, 1));
}

TEST(SPSCRingBufferTest, IsFull) {
    SPSCRingBuffer<int, 4> buf;
    std::vector<int> data(7);
    buf.writeExact(data.data(), 7);
    EXPECT_TRUE(buf.isFull());
    EXPECT_EQ(buf.available(), 0u);
}

TEST(SPSCRingBufferTest, WrapAroundWrite) {
    SPSCRingBuffer<int, 8> buf;
    std::vector<int> data(15);
    for (size_t i = 0; i < 15; ++i) {
        data[i] = static_cast<int>(i);
    }
    buf.writeExact(data.data(), 15);

    int readData[5];
    buf.readExact(readData, 5);

    std::vector<int> newData(5);
    for (size_t i = 0; i < 5; ++i) {
        newData[i] = static_cast<int>(100 + i);
    }
    EXPECT_TRUE(buf.writeExact(newData.data(), 5));

    EXPECT_TRUE(buf.isFull());

    int allData[15];
    buf.readExact(allData, 15);
    for (size_t i = 0; i < 10; ++i) {
        EXPECT_EQ(allData[i], 5 + i);
    }
    for (size_t i = 0; i < 5; ++i) {
        EXPECT_EQ(allData[10 + i], 100 + i);
    }
}

TEST(SPSCRingBufferTest, WrapAroundRead) {
    SPSCRingBuffer<int, 8> buf;
    std::vector<int> data(8);
    for (size_t i = 0; i < 8; ++i) {
        data[i] = static_cast<int>(i);
    }
    buf.writeExact(data.data(), 8);

    int readData[3];
    buf.readExact(readData, 3);

    std::vector<int> newData(3);
    for (size_t i = 0; i < 3; ++i) {
        newData[i] = static_cast<int>(100 + i);
    }
    buf.writeExact(newData.data(), 3);

    int allData[8];
    buf.readExact(allData, 8);
    EXPECT_EQ(allData[0], 3);
    EXPECT_EQ(allData[4], 7);
    for (size_t i = 0; i < 3; ++i) {
        EXPECT_EQ(allData[5 + i], 100 + i);
    }
}

TEST(SPSCRingBufferTest, Reset) {
    SPSCRingBuffer<int, 8> buf;
    int data[] = {1, 2, 3};
    buf.writeExact(data, 3);
    EXPECT_EQ(buf.length(), 3u);

    buf.reset();
    EXPECT_TRUE(buf.isEmpty());
    EXPECT_EQ(buf.length(), 0u);
    EXPECT_EQ(buf.available(), buf.capacity());
}

TEST(SPSCRingBufferTest, LengthAndAvailable) {
    SPSCRingBuffer<int, 16> buf;
    EXPECT_EQ(buf.length(), 0u);
    EXPECT_EQ(buf.available(), 31u);

    std::vector<int> data(5);
    buf.writeExact(data.data(), 5);
    EXPECT_EQ(buf.length(), 5u);
    EXPECT_EQ(buf.available(), 26u);

    int readData[2];
    buf.readExact(readData, 2);
    EXPECT_EQ(buf.length(), 3u);
    EXPECT_EQ(buf.available(), 28u);
}

TEST(SPSCRingBufferTest, SingleElementBuffer) {
    SPSCRingBuffer<int, 1> buf;
    EXPECT_TRUE(buf.isEmpty());
    EXPECT_FALSE(buf.isFull());

    int v = 42;
    EXPECT_TRUE(buf.writeExact(&v, 1));
    EXPECT_TRUE(buf.isFull());
    EXPECT_EQ(buf.length(), 1u);

    int out = 0;
    EXPECT_TRUE(buf.readExact(&out, 1));
    EXPECT_EQ(out, 42);
    EXPECT_TRUE(buf.isEmpty());
}

TEST(SPSCRingBufferTest, SPSCConcurrent) {
    static constexpr int NUM_ITEMS = 100000;
    SPSCRingBuffer<int, 1024> buf;
    std::atomic<int> totalRead{0};
    std::atomic<bool> producerDone{false};

    std::thread producer([&]() {
        for (int i = 1; i <= NUM_ITEMS; ++i) {
            while (!buf.writeExact(&i, 1)) {
                std::this_thread::yield();
            }
        }
        producerDone.store(true, std::memory_order_release);
    });

    std::thread consumer([&]() {
        int value = 0;
        while (!producerDone.load(std::memory_order_acquire) || buf.length() > 0) {
            if (buf.readExact(&value, 1)) {
                totalRead.fetch_add(1, std::memory_order_relaxed);
            } else {
                std::this_thread::yield();
            }
        }
    });

    producer.join();
    consumer.join();

    EXPECT_EQ(totalRead.load(), NUM_ITEMS);
}

TEST(SPSCRingBufferTest, MemoryOrdering) {
    static constexpr int NUM_ITEMS = 50000;
    SPSCRingBuffer<int, 512> buf;
    std::atomic<int> sum{0};
    std::atomic<bool> done{false};

    std::thread writer([&]() {
        for (int i = 1; i <= NUM_ITEMS; ++i) {
            while (!buf.writeExact(&i, 1)) {
                std::this_thread::yield();
            }
        }
    });

    std::thread reader([&]() {
        int value = 0;
        int count = 0;
        while (count < NUM_ITEMS) {
            if (buf.readExact(&value, 1)) {
                sum.fetch_add(value, std::memory_order_relaxed);
                ++count;
            } else {
                std::this_thread::yield();
            }
        }
        done.store(true, std::memory_order_release);
    });

    writer.join();
    reader.join();

    int64_t expected = int64_t(NUM_ITEMS) * (NUM_ITEMS + 1) / 2;
    EXPECT_EQ(sum.load(), expected);
}

TEST(SPSCRingBufferTest, Uint8Specialization) {
    SPSCRingBuffer<uint8_t, 256> buf;
    std::vector<uint8_t> data(511);
    for (size_t i = 0; i < 511; ++i) {
        data[i] = static_cast<uint8_t>(i);
    }

    EXPECT_TRUE(buf.writeExact(data.data(), 511));
    EXPECT_TRUE(buf.isFull());

    std::vector<uint8_t> readData(511);
    EXPECT_TRUE(buf.readExact(readData.data(), 511));
    for (size_t i = 0; i < 511; ++i) {
        EXPECT_EQ(readData[i], static_cast<uint8_t>(i));
    }
}

TEST(SPSCRingBufferTest, ResetAndReuse) {
    SPSCRingBuffer<int, 8> buf;
    std::vector<int> data(15);
    for (size_t i = 0; i < 15; ++i) {
        data[i] = static_cast<int>(i);
    }
    buf.writeExact(data.data(), 15);
    EXPECT_TRUE(buf.isFull());

    buf.reset();
    EXPECT_TRUE(buf.isEmpty());
    EXPECT_TRUE(buf.writeExact(data.data(), 15));
    EXPECT_TRUE(buf.isFull());
}

TEST(SPSCRingBufferTest, ZeroRequestBecomesOne) {
    SPSCRingBuffer<int, 0> buf;
    EXPECT_EQ(buf.capacity(), 1u);
}

}
