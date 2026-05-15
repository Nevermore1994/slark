//
// SPSCRingBufferTest.cpp
// Unit tests for SPSCRingBuffer (lock-free single-producer single-consumer ring buffer)
//

#include "SPSCRingBuffer.hpp"

#include <atomic>
#include <gtest/gtest.h>
#include <thread>
#include <vector>

namespace slark {

// N 不是 2 的幂，验证 kCapacity = bit_ceil(N+1)，capacity() = kCapacity - 1
TEST(SPSCRingBufferTest, CapacityIsPowerOfTwo) {
    SPSCRingBuffer<int, 10> buf; // bit_ceil(10+1)=16, capacity=15
    EXPECT_EQ(buf.capacity(), 15u);
}

TEST(SPSCRingBufferTest, CapacityExactPowerOfTwo) {
    SPSCRingBuffer<int, 16> buf; // bit_ceil(16+1)=32, capacity=31
    EXPECT_EQ(buf.capacity(), 31u);
}

TEST(SPSCRingBufferTest, CapacityOne) {
    SPSCRingBuffer<int, 1> buf; // bit_ceil(1) = 1
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
    SPSCRingBuffer<int, 4> buf; // bit_ceil(4+1)=8, capacity=7
    std::vector<int> data(7);
    EXPECT_TRUE(buf.writeExact(data.data(), 7)); // 刚好写满
    EXPECT_TRUE(buf.isFull());
    int extra = 1;
    EXPECT_FALSE(buf.writeExact(&extra, 1)); // 满，写入失败
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
    // 数据未被消费
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
    SPSCRingBuffer<int, 4> buf; // bit_ceil(4+1)=8, capacity=7
    std::vector<int> data(7);
    buf.writeExact(data.data(), 7);
    EXPECT_TRUE(buf.isFull());
    EXPECT_EQ(buf.available(), 0u);
}

TEST(SPSCRingBufferTest, WrapAroundWrite) {
    SPSCRingBuffer<int, 8> buf; // bit_ceil(8+1)=16, capacity=15
    // 写满再读一部分，触发写指针回绕
    std::vector<int> data(15);
    for (int i = 0; i < 15; ++i) {
        data[i] = i;
    }
    buf.writeExact(data.data(), 15);

    int readData[5];
    buf.readExact(readData, 5); // 读 5 个，剩 10 个

    // 再写 5 个，写指针回绕
    std::vector<int> newData(5);
    for (int i = 0; i < 5; ++i) {
        newData[i] = 100 + i;
    }
    EXPECT_TRUE(buf.writeExact(newData.data(), 5));

    // 现在 buffer 里有 10 + 5 = 15 个（满）
    EXPECT_TRUE(buf.isFull());

    // 全部读出验证
    int allData[15];
    buf.readExact(allData, 15);
    // 前 10 个是原来的 data[5..14]
    for (int i = 0; i < 10; ++i) {
        EXPECT_EQ(allData[i], 5 + i);
    }
    // 后 5 个是 newData
    for (int i = 0; i < 5; ++i) {
        EXPECT_EQ(allData[10 + i], 100 + i);
    }
}

TEST(SPSCRingBufferTest, WrapAroundRead) {
    SPSCRingBuffer<int, 8> buf;
    // 写入 8 个，读出 3 个，再写入 3 个触发回绕，然后验证读出顺序正确
    std::vector<int> data(8);
    for (int i = 0; i < 8; ++i) {
        data[i] = i;
    }
    buf.writeExact(data.data(), 8);

    int readData[3];
    buf.readExact(readData, 3); // readPos 前进 3

    std::vector<int> newData(3);
    for (int i = 0; i < 3; ++i) {
        newData[i] = 100 + i;
    }
    buf.writeExact(newData.data(), 3); // 写指针回绕

    int allData[8];
    buf.readExact(allData, 8);
    // 前 5 个是 data[3..7]
    EXPECT_EQ(allData[0], 3);
    EXPECT_EQ(allData[4], 7);
    // 后 3 个是 newData
    for (int i = 0; i < 3; ++i) {
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
    SPSCRingBuffer<int, 16> buf; // bit_ceil(16+1)=32, capacity=31
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

// SPSC 并发测试：单生产者单消费者
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

// 验证 writeExact 和 readExact 的内存顺序正确性（acquire/release）
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

    // 验证 1+2+...+NUM_ITEMS
    int64_t expected = int64_t(NUM_ITEMS) * (NUM_ITEMS + 1) / 2;
    EXPECT_EQ(sum.load(), expected);
}

// 测试 uint8_t 特化（AudioRenderComponent 实际使用场景）
TEST(SPSCRingBufferTest, Uint8Specialization) {
    SPSCRingBuffer<uint8_t, 256> buf; // bit_ceil(257)=512, capacity=511
    std::vector<uint8_t> data(511);
    for (int i = 0; i < 511; ++i) {
        data[i] = static_cast<uint8_t>(i);
    }

    EXPECT_TRUE(buf.writeExact(data.data(), 511));
    EXPECT_TRUE(buf.isFull());

    std::vector<uint8_t> readData(511);
    EXPECT_TRUE(buf.readExact(readData.data(), 511));
    for (int i = 0; i < 511; ++i) {
        EXPECT_EQ(readData[i], static_cast<uint8_t>(i));
    }
}

// 测试 reset 后 epoch 语义：reset 后读写指针归零，buffer 可重新使用
TEST(SPSCRingBufferTest, ResetAndReuse) {
    SPSCRingBuffer<int, 8> buf; // bit_ceil(8+1)=16, capacity=15
    std::vector<int> data(15);
    for (int i = 0; i < 15; ++i) {
        data[i] = i;
    }
    buf.writeExact(data.data(), 15);
    EXPECT_TRUE(buf.isFull());

    buf.reset();
    EXPECT_TRUE(buf.isEmpty());
    EXPECT_TRUE(buf.writeExact(data.data(), 15));
    EXPECT_TRUE(buf.isFull());
}

// 测试 capacity 为 0 的编译期保护（N=0 时 bit_ceil(0)=1，capacity=1）
TEST(SPSCRingBufferTest, ZeroRequestBecomesOne) {
    SPSCRingBuffer<int, 0> buf; // std::bit_ceil(0) = 1
    EXPECT_EQ(buf.capacity(), 1u);
}

} // namespace slark
