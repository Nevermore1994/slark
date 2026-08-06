//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//
#include "RingBuffer.hpp"

#include <gtest/gtest.h>

#include <thread>
#include <vector>

namespace slark {

class RingBufferTest : public testing::Test {
 protected:
    RingBuffer<int, 10> buffer;
    static constexpr int TEST_CAPACITY = 16;

    void SetUp() override {}
};

TEST_F(RingBufferTest, InitialState) {
    EXPECT_TRUE(buffer.isEmpty());
    EXPECT_FALSE(buffer.isFull());
    EXPECT_EQ(buffer.length(), 0u);
    EXPECT_EQ(buffer.capacity(), TEST_CAPACITY);
    EXPECT_EQ(buffer.available(), TEST_CAPACITY);
}

TEST_F(RingBufferTest, WriteExactAndReadExact) {
    int data[] = {1, 2, 3, 4, 5};
    EXPECT_TRUE(buffer.writeExact(data, 5));
    EXPECT_EQ(buffer.length(), 5u);

    int readData[5] = {0};
    EXPECT_TRUE(buffer.readExact(readData, 5));
    for (int i = 0; i < 5; ++i) {
        EXPECT_EQ(readData[i], data[i]);
    }
}

TEST_F(RingBufferTest, WriteExactFail) {
    int data[] = {1, 2, 3};
    EXPECT_TRUE(buffer.writeExact(data, 3));
    EXPECT_EQ(buffer.length(), 3u);
    EXPECT_FALSE(buffer.writeExact(data, TEST_CAPACITY));
}

TEST_F(RingBufferTest, ReadExactFail) {
    int data[] = {1, 2, 3};
    EXPECT_TRUE(buffer.writeExact(data, 3));

    int readData[3] = {0};
    EXPECT_TRUE(buffer.readExact(readData, 3));
    for (int i = 0; i < 3; ++i) {
        EXPECT_EQ(readData[i], data[i]);
    }
    EXPECT_FALSE(buffer.readExact(readData, 1));
}

TEST_F(RingBufferTest, WrapAround) {
    std::vector<int> data(TEST_CAPACITY - 2);
    for (size_t i = 0; i < TEST_CAPACITY - 2; ++i) {
        data[i] = static_cast<int>(i);
    }

    EXPECT_TRUE(buffer.writeExact(data.data(), static_cast<uint32_t>(data.size())));

    std::vector<int> readData(TEST_CAPACITY / 2);
    EXPECT_TRUE(buffer.readExact(readData.data(), TEST_CAPACITY / 2));

    int newData[] = {100, 101, 102};
    EXPECT_TRUE(buffer.writeExact(newData, 3));

    auto remainSize = buffer.length();
    std::vector<int> allData(remainSize);
    EXPECT_TRUE(buffer.readExact(allData.data(), remainSize));
    EXPECT_TRUE(buffer.isEmpty());
}

TEST_F(RingBufferTest, EdgeCases) {
    EXPECT_FALSE(buffer.writeExact(nullptr, 5));
    EXPECT_FALSE(buffer.readExact(nullptr, 5));

    int data[1];
    EXPECT_TRUE(buffer.writeExact(data, 0));
    EXPECT_TRUE(buffer.readExact(data, 0));

    std::vector<int> fillData(TEST_CAPACITY);
    for (size_t i = 0; i < TEST_CAPACITY; ++i) {
        fillData[i] = static_cast<int>(i);
    }
    EXPECT_TRUE(buffer.writeExact(fillData.data(), TEST_CAPACITY));
    EXPECT_TRUE(buffer.isFull());

    int extraData[] = {100};
    EXPECT_FALSE(buffer.writeExact(extraData, 1));

    std::vector<int> largeBuffer(TEST_CAPACITY + 5);
    EXPECT_FALSE(buffer.readExact(largeBuffer.data(), static_cast<uint32_t>(largeBuffer.size())));

    buffer.reset();
    EXPECT_FALSE(buffer.readExact(largeBuffer.data(), 5));
}

TEST_F(RingBufferTest, Reset) {
    int data[] = {1, 2, 3};
    EXPECT_TRUE(buffer.writeExact(data, 3));

    buffer.reset();
    EXPECT_TRUE(buffer.isEmpty());
    EXPECT_EQ(buffer.length(), 0u);
    EXPECT_EQ(buffer.available(), TEST_CAPACITY);

    EXPECT_TRUE(buffer.writeExact(data, 3));
}

TEST_F(RingBufferTest, FullBuffer) {
    std::vector<int> data(TEST_CAPACITY);
    for (size_t i = 0; i < TEST_CAPACITY; ++i) {
        data[i] = static_cast<int>(i);
    }

    EXPECT_TRUE(buffer.writeExact(data.data(), TEST_CAPACITY));
    EXPECT_TRUE(buffer.isFull());
    EXPECT_EQ(buffer.available(), 0u);

    int extraData = 100;
    EXPECT_FALSE(buffer.writeExact(&extraData, 1));
}


class SyncRingBufferTest : public ::testing::Test {
 protected:
    SyncRingBuffer<int, 1024> buffer;
};

TEST_F(SyncRingBufferTest, InitialState) {
    EXPECT_TRUE(buffer.isEmpty());
    EXPECT_FALSE(buffer.isFull());
    EXPECT_EQ(buffer.length(), 0u);
    EXPECT_EQ(buffer.capacity(), 1024u);
    EXPECT_EQ(buffer.available(), 1024u);
}

TEST_F(SyncRingBufferTest, WriteExactAndReadExact) {
    int data[] = {1, 2, 3, 4, 5};
    EXPECT_TRUE(buffer.writeExact(data, 5));
    EXPECT_EQ(buffer.length(), 5u);

    int readData[5] = {0};
    EXPECT_TRUE(buffer.readExact(readData, 5));
    for (int i = 0; i < 5; ++i) {
        EXPECT_EQ(readData[i], data[i]);
    }
}

TEST_F(SyncRingBufferTest, WriteExactFail) {
    int data[] = {1, 2, 3};
    EXPECT_TRUE(buffer.writeExact(data, 3));
    EXPECT_EQ(buffer.length(), 3u);
    EXPECT_FALSE(buffer.writeExact(data, 1024));
}

TEST_F(SyncRingBufferTest, ReadExactFail) {
    int data[] = {1, 2, 3};
    EXPECT_TRUE(buffer.writeExact(data, 3));

    int readData[3] = {0};
    EXPECT_TRUE(buffer.readExact(readData, 3));
    for (int i = 0; i < 3; ++i) {
        EXPECT_EQ(readData[i], data[i]);
    }
    EXPECT_FALSE(buffer.readExact(readData, 1));
}

TEST_F(SyncRingBufferTest, WrapAround) {
    constexpr uint32_t CAP = 1024;
    std::vector<int> data(CAP - 2);
    for (size_t i = 0; i < CAP - 2; ++i) {
        data[i] = static_cast<int>(i);
    }

    EXPECT_TRUE(buffer.writeExact(data.data(), static_cast<uint32_t>(data.size())));

    std::vector<int> readData(CAP / 2);
    EXPECT_TRUE(buffer.readExact(readData.data(), CAP / 2));

    int newData[] = {100, 101, 102};
    EXPECT_TRUE(buffer.writeExact(newData, 3));

    auto remainSize = buffer.length();
    std::vector<int> allData(remainSize);
    EXPECT_TRUE(buffer.readExact(allData.data(), remainSize));
    EXPECT_TRUE(buffer.isEmpty());
}

TEST_F(SyncRingBufferTest, EdgeCases) {
    EXPECT_FALSE(buffer.writeExact(nullptr, 5));
    EXPECT_FALSE(buffer.readExact(nullptr, 5));

    int data[1];
    EXPECT_TRUE(buffer.writeExact(data, 0));
    EXPECT_TRUE(buffer.readExact(data, 0));
}

TEST_F(SyncRingBufferTest, Reset) {
    int data[] = {1, 2, 3};
    EXPECT_TRUE(buffer.writeExact(data, 3));

    buffer.reset();
    EXPECT_TRUE(buffer.isEmpty());
    EXPECT_EQ(buffer.length(), 0u);
    EXPECT_EQ(buffer.available(), 1024u);

    EXPECT_TRUE(buffer.writeExact(data, 3));
}

TEST_F(SyncRingBufferTest, FullBuffer) {
    std::vector<int> data(1024);
    for (size_t i = 0; i < 1024; ++i) {
        data[i] = static_cast<int>(i);
    }

    EXPECT_TRUE(buffer.writeExact(data.data(), 1024));
    EXPECT_TRUE(buffer.isFull());
    EXPECT_EQ(buffer.available(), 0u);

    int extraData = 100;
    EXPECT_FALSE(buffer.writeExact(&extraData, 1));
}

TEST_F(SyncRingBufferTest, ConcurrentWriteRead) {
    static constexpr int NUM_ITEMS = 10000;
    std::atomic<bool> startFlag{false};
    std::atomic<int> readSum{0};
    std::atomic<int> writeSum{0};

    std::thread producer([&]() {
        while (!startFlag.load()) {
        }
        for (int i = 1; i <= NUM_ITEMS; ++i) {
            while (!buffer.writeExact(&i, 1)) {
                std::this_thread::yield();
            }
            writeSum += i;
        }
    });

    std::thread consumer([&]() {
        while (!startFlag.load()) {
        }
        int count = 0;
        while (count < NUM_ITEMS) {
            int value;
            if (buffer.readExact(&value, 1)) {
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
        while (!startFlag.load()) {
        }
        std::vector<int> data(BATCH_SIZE);
        for (int batch = 0; batch < NUM_BATCHES; ++batch) {
            for (size_t i = 0; i < BATCH_SIZE; ++i) {
                data[i] = static_cast<int>(static_cast<size_t>(batch * BATCH_SIZE) + i + 1);
            }
            while (!buffer.writeExact(data.data(), BATCH_SIZE)) {
                std::this_thread::yield();
            }
            writeCount += BATCH_SIZE;
        }
    });

    std::thread consumer([&]() {
        while (!startFlag.load()) {
        }
        std::vector<int> data(BATCH_SIZE);
        while (readCount < NUM_BATCHES * BATCH_SIZE) {
            if (buffer.readExact(data.data(), BATCH_SIZE)) {
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
        while (!startFlag.load()) {
        }
        for (int i = 1; i <= NUM_ITERATIONS && !stopFlag.load(); ++i) {
            if (buffer.writeExact(&i, 1)) {
                totalWritten++;
            }
            std::this_thread::yield();
        }
    });

    std::thread consumer([&]() {
        while (!startFlag.load()) {
        }
        int value;
        while (!stopFlag.load() || !buffer.isEmpty()) {
            if (buffer.readExact(&value, 1)) {
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
            while (!startFlag.load()) {
            }
            for (size_t i = 0; i < ITEMS_PER_PRODUCER; ++i) {
                int value = static_cast<int>(static_cast<size_t>(p * ITEMS_PER_PRODUCER) + i + 1);
                while (!buffer.writeExact(&value, 1)) {
                    std::this_thread::yield();
                }
                totalWritten.fetch_add(1);
            }
        });
    }

    for (int c = 0; c < NUM_CONSUMERS; ++c) {
        consumers.emplace_back([&]() {
            while (!startFlag.load()) {
            }
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

TEST_F(SyncRingBufferTest, ResetDuringUse) {
    static constexpr int NUM_ITERATIONS = 100000;
    std::atomic<bool> startFlag{false};
    std::atomic<bool> stopFlag{false};
    std::atomic<uint64_t> totalWritten{0};
    std::atomic<uint64_t> totalRead{0};

    std::thread producer([&]() {
        while (!startFlag.load()) {
        }
        for (int i = 1; i <= NUM_ITERATIONS && !stopFlag.load(); ++i) {
            if (buffer.writeExact(&i, 1)) {
                totalWritten++;
            }
            if (i % 20 == 0) {
                buffer.reset();
            }
            std::this_thread::yield();
        }
    });

    std::thread consumer([&]() {
        while (!startFlag.load()) {
        }
        int value;
        while (!stopFlag.load() || !buffer.isEmpty()) {
            if (buffer.readExact(&value, 1)) {
                totalRead++;
            } else {
                std::this_thread::yield();
            }
        }
    });

    startFlag.store(true);
    std::this_thread::sleep_for(std::chrono::seconds(2));
    stopFlag.store(true);

    producer.join();
    consumer.join();
}


TEST_F(RingBufferTest, SpanWriteAndRead) {
    std::vector<int> data = {10, 20, 30, 40, 50};
    EXPECT_TRUE(buffer.writeExact(std::span<const int>(data)));
    EXPECT_EQ(buffer.length(), 5u);

    std::vector<int> readData(5);
    EXPECT_TRUE(buffer.readExact(std::span<int>(readData)));
    for (size_t i = 0; i < 5; ++i) {
        EXPECT_EQ(readData[i], data[i]);
    }
}

TEST_F(RingBufferTest, PeekDoesNotMoveReadPointer) {
    int data[] = {1, 2, 3, 4, 5};
    EXPECT_TRUE(buffer.writeExact(data, 5));

    int peekData[3] = {0};
    EXPECT_EQ(buffer.peek(peekData, 3), 3u);
    EXPECT_EQ(peekData[0], 1);
    EXPECT_EQ(peekData[1], 2);
    EXPECT_EQ(peekData[2], 3);
    EXPECT_EQ(buffer.length(), 5u);

    int readData[5] = {0};
    EXPECT_TRUE(buffer.readExact(readData, 5));
    for (size_t i = 0; i < 5; ++i) {
        EXPECT_EQ(readData[i], data[i]);
    }
}

TEST_F(RingBufferTest, PeekWithSpan) {
    std::vector<int> data = {10, 20, 30};
    EXPECT_TRUE(buffer.writeExact(std::span<const int>(data)));

    std::vector<int> peekData(2);
    EXPECT_EQ(buffer.peek(std::span<int>(peekData)), 2u);
    EXPECT_EQ(peekData[0], 10);
    EXPECT_EQ(peekData[1], 20);
    EXPECT_EQ(buffer.length(), 3u);
}

TEST_F(RingBufferTest, PeekEmpty) {
    int peekData[3] = {0};
    EXPECT_EQ(buffer.peek(peekData, 3), 0u);
}


TEST_F(SyncRingBufferTest, TimeoutWriteExactImmediateSuccess) {
    int data[] = {1, 2, 3};
    EXPECT_TRUE(buffer.writeExact(data, 3, 0));
    EXPECT_EQ(buffer.length(), 3u);
}

TEST_F(SyncRingBufferTest, TimeoutWriteExactImmediateFail) {
    std::vector<int> data(1024);
    EXPECT_TRUE(buffer.writeExact(data.data(), 1024));
    int extra = 1;
    EXPECT_FALSE(buffer.writeExact(&extra, 1, 0));
}

TEST_F(SyncRingBufferTest, CompileTimeCapacity) {
    slark::RingBuffer<int, 10> rb;
    slark::SyncRingBuffer<int, 10> srb;
    EXPECT_EQ(rb.capacity(), 16u);
    EXPECT_EQ(srb.capacity(), 16u);
}

}
