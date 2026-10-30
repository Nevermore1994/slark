//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#include "Writer.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <latch>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>

using namespace slark;
using namespace std::chrono_literals;

namespace {
std::filesystem::path makePath(std::string_view suffix) {
    static std::atomic<uint64_t> counter{0};
    const auto ticks = std::chrono::steady_clock::now().time_since_epoch().count();
    return std::filesystem::temp_directory_path() /
           ("slark_writer_" + std::to_string(ticks) + "_" + std::to_string(counter++) + "_" +
            std::string(suffix));
}

std::string readFile(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
}

void removeFile(const std::filesystem::path& path) {
    std::error_code error;
    std::filesystem::remove(path, error);
}
}  // namespace

TEST(WriterTest, CloseWaitsForAllPreviouslyAcceptedData) {
    const auto path = makePath("close");
    Writer writer;
    ASSERT_TRUE(writer.open(path.string()));
    ASSERT_TRUE(writer.write("first\n"));
    ASSERT_TRUE(writer.write("second\n"));
    writer.close();

    EXPECT_EQ(readFile(path), "first\nsecond\n");
    EXPECT_FALSE(writer.isOpen());
    writer.dispose();
    removeFile(path);
}

TEST(WriterTest, OpenReplacementPreservesFileOwnership) {
    const auto oldPath = makePath("old");
    const auto newPath = makePath("new");
    Writer writer;
    ASSERT_TRUE(writer.open(oldPath.string()));
    ASSERT_TRUE(writer.write("old-data\n"));
    ASSERT_TRUE(writer.open(newPath.string()));
    ASSERT_TRUE(writer.write("new-data\n"));
    writer.close();

    EXPECT_EQ(readFile(oldPath), "old-data\n");
    EXPECT_EQ(readFile(newPath), "new-data\n");
    writer.dispose();
    removeFile(oldPath);
    removeFile(newPath);
}

TEST(WriterTest, PreservesDataWrittenBeforeOpenAndRejectsNull) {
    const auto path = makePath("parked");
    Writer writer;
    EXPECT_FALSE(writer.write(DataPtr{}));
    ASSERT_TRUE(writer.write("parked-data\n"));
    ASSERT_FALSE(writer.open("/no/such/slark-writer-directory/file.log"));
    ASSERT_TRUE(writer.open(path.string()));
    writer.close();

    EXPECT_EQ(readFile(path), "parked-data\n");
    writer.dispose();
    removeFile(path);
}

TEST(WriterTest, ConcurrentWriteAndDisposeAccountForEveryAcceptedItem) {
    const auto path = makePath("dispose");
    Writer writer;
    ASSERT_TRUE(writer.open(path.string()));

    constexpr int kWriters = 8;
    constexpr int kItemsPerWriter = 100;
    std::latch start{kWriters + 2};
    std::mutex acceptedMutex;
    std::vector<std::string> accepted;
    std::vector<std::thread> writers;
    for (int writerIndex = 0; writerIndex < kWriters; ++writerIndex) {
        writers.emplace_back([&, writerIndex] {
            start.count_down();
            start.wait();
            for (int itemIndex = 0; itemIndex < kItemsPerWriter; ++itemIndex) {
                const auto item =
                    std::to_string(writerIndex) + ":" + std::to_string(itemIndex) + "\n";
                if (writer.write(item)) {
                    std::lock_guard lock(acceptedMutex);
                    accepted.push_back(item);
                }
            }
        });
    }

    std::thread firstDispose([&] {
        start.count_down();
        start.wait();
        writer.dispose();
    });
    std::thread secondDispose([&] {
        start.count_down();
        start.wait();
        writer.dispose();
    });
    for (auto& thread : writers) {
        thread.join();
    }
    firstDispose.join();
    secondDispose.join();

    EXPECT_FALSE(writer.write("after-dispose\n"));
    EXPECT_FALSE(writer.open(path.string()));
    writer.close();

    std::ifstream input(path);
    std::vector<std::string> contents;
    for (std::string line; std::getline(input, line);) {
        contents.push_back(line + "\n");
    }
    EXPECT_EQ(contents.size(), accepted.size());
    for (const auto& item : accepted) {
        EXPECT_EQ(std::count(contents.begin(), contents.end(), item), 1);
    }
    removeFile(path);
}

TEST(WriterTest, MultiProducerPreservesPerProducerOrder) {
    const auto path = makePath("w1");
    Writer writer;
    ASSERT_TRUE(writer.open(path.string()));

    constexpr int kProducers = 8;
    constexpr int kItemsPerProducer = 2000;
    std::vector<std::vector<std::string>> expected(static_cast<size_t>(kProducers));
    std::vector<std::thread> producers;
    for (int producer = 0; producer < kProducers; ++producer) {
        producers.emplace_back([&, producer] {
            for (int item = 0; item < kItemsPerProducer; ++item) {
                auto line = std::to_string(producer) + ":" + std::to_string(item) + "\n";
                EXPECT_TRUE(writer.write(line));
                expected[static_cast<size_t>(producer)].push_back(std::move(line));
            }
        });
    }
    for (auto& producer : producers) {
        producer.join();
    }
    writer.close();

    // Per-producer subsequence must appear in order inside the merged file.
    std::vector<size_t> positions(static_cast<size_t>(kProducers), 0);
    std::ifstream input(path);
    for (std::string line; std::getline(input, line);) {
        line += "\n";
        const auto sep = line.find(':');
        ASSERT_NE(sep, std::string::npos);
        const auto producer = static_cast<size_t>(std::stoi(line.substr(0, sep)));
        const auto& sequence = expected[producer];
        ASSERT_LT(positions[producer], sequence.size());
        EXPECT_EQ(line, sequence[positions[producer]]);
        ++positions[producer];
    }
    for (int producer = 0; producer < kProducers; ++producer) {
        EXPECT_EQ(positions[static_cast<size_t>(producer)], kItemsPerProducer);
    }
    writer.dispose();
    removeFile(path);
}

TEST(WriterTest, DisposeLoopDrainsEverythingAccepted) {
    const auto path = makePath("w4");
    constexpr int kRounds = 200;
    for (int round = 0; round < kRounds; ++round) {
        Writer writer;
        ASSERT_TRUE(writer.open(path.string())) << "round " << round;
        const auto payload = "round-" + std::to_string(round) + "\n";
        for (int item = 0; item < 32; ++item) {
            ASSERT_TRUE(writer.write(payload));
        }
        writer.dispose();  // no explicit close: dispose must flush accepted data
        auto content = readFile(path);
        EXPECT_EQ(content.size(), payload.size() * 32) << "round " << round;
        removeFile(path);
    }
}

TEST(WriterTest, WriteCloseOpenRaceOwnershipByCallInterval) {
    const auto oldPath = makePath("w7-old");
    const auto newPath = makePath("w7-new");
    Writer writer;
    ASSERT_TRUE(writer.open(oldPath.string()));

    constexpr int kRounds = 20000;
    std::atomic<bool> stopWriter{false};
    std::atomic<int> accepted{0};
    std::atomic<bool> closeCalled{false};
    std::atomic<bool> closeReturned{false};

    std::thread producer([&] {
        for (int i = 0; i < kRounds && !stopWriter.load(); ++i) {
            const auto item = std::to_string(i) + "\n";
            if (writer.write(item)) {
                ++accepted;
            }
        }
    });

    std::this_thread::sleep_for(5ms);
    closeCalled.store(true);
    writer.close();
    closeReturned.store(true);
    ASSERT_TRUE(writer.open(newPath.string()));
    stopWriter.store(true);
    producer.join();

    // Writes that returned before close() was called must be in the old file;
    // writes made after close() returned must not be.
    const auto oldContent = readFile(oldPath);
    size_t position = 0;
    int lastOld = -1;
    while (position < oldContent.size()) {
        const auto end = oldContent.find('\n', position);
        ASSERT_NE(end, std::string::npos);
        lastOld = std::stoi(oldContent.substr(position, end - position));
        position = end + 1;
    }
    // Every index up to lastOld appears in the old file and none greater does:
    // parse all indices and assert the old file is exactly {0..lastOld}.
    std::set<int> oldIndices;
    position = 0;
    while (position < oldContent.size()) {
        const auto end = oldContent.find('\n', position);
        oldIndices.insert(std::stoi(oldContent.substr(position, end - position)));
        position = end + 1;
    }
    if (!oldIndices.empty()) {
        EXPECT_EQ(*oldIndices.begin(), 0);
        EXPECT_EQ(static_cast<size_t>(*oldIndices.rbegin()), oldIndices.size() - 1);
        EXPECT_GE(lastOld, 0);
    }
    // New file must contain a contiguous, strictly increasing run starting above lastOld.
    const auto newContent = readFile(newPath);
    position = 0;
    int previous = lastOld;
    while (position < newContent.size()) {
        const auto end = newContent.find('\n', position);
        const auto value = std::stoi(newContent.substr(position, end - position));
        EXPECT_GT(value, previous) << "order violated across files";
        previous = value;
        position = end + 1;
    }
    writer.dispose();
    removeFile(oldPath);
    removeFile(newPath);
}

TEST(WriterTest, ParkedDataSurvivesFailedOpenAndIsDroppedOnDisposeWithoutFile) {
    const auto path = makePath("w11");
    Writer writer;
    for (int i = 0; i < 16; ++i) {
        ASSERT_TRUE(writer.write("item-" + std::to_string(i) + "\n"));
    }
    ASSERT_FALSE(writer.open("/no/such/slark-writer-directory/file.log"));

    // Failed open must not touch parked data: a later successful open flushes it.
    ASSERT_TRUE(writer.open(path.string()));
    writer.close();
    for (int i = 0; i < 16; ++i) {
        EXPECT_NE(readFile(path).find("item-" + std::to_string(i) + "\n"), std::string::npos);
    }
    removeFile(path);

    // Second scenario: never opened, dispose drops the parked data (logged via LogP).
    const auto untouched = makePath("w11-untouched");
    Writer writer2;
    for (int i = 0; i < 8; ++i) {
        ASSERT_TRUE(writer2.write("dropped-" + std::to_string(i) + "\n"));
    }
    writer2.dispose();
    EXPECT_FALSE(std::filesystem::exists(untouched));
}

TEST(WriterTest, ParkedDataBeforeOpenIsBoundedAndKeepsTheNewest) {
    const auto path = makePath("parked-bounded");
    Writer writer;
    constexpr size_t kChunk = 1024 * 1024;
    for (char c = 'a'; c <= 'h'; ++c) {
        ASSERT_TRUE(writer.write(std::string(kChunk, c)));
    }
    ASSERT_TRUE(writer.open(path.string()));
    writer.close();

    const auto content = readFile(path);
    EXPECT_EQ(content.size(), Writer::kMaxParkedBytes);
    EXPECT_EQ(content.front(), 'e');
    EXPECT_EQ(content.back(), 'h');
    writer.dispose();
    removeFile(path);
}
