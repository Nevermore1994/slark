//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//
#include <gtest/gtest.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <format>
#include <latch>
#include <memory>
#include <mutex>
#include <numeric>
#include <print>
#include <queue>
#include <string>
#include <thread>
#include <vector>

#include "Data.h"
#include "FileUtil.h"
#include "MPSCQueue.hpp"
#include "Synchronized.hpp"
#include "Time.hpp"
#include "Writer.h"

using namespace slark;
using namespace std::chrono;

namespace {

static std::string genLogLine(int32_t threadId, int32_t seq) {
    return std::format("2025-01-01 12:00:00.000 [info] [OrderedBench.cpp][Line:{}][Function:test] "
                       "thread={} seq={} benchmark log message padding padding padding\n",
                       seq, threadId, seq);
}

static std::string benchLogDir() {
    const std::string dir = File::cachePath() + "/ordered_bench_logs";
    if (!File::isDirExist(dir)) {
        File::createDir(dir);
    }
    return dir;
}

static void cleanBenchDir() {
    const std::string dir = File::cachePath() + "/ordered_bench_logs";
    if (File::isDirExist(dir)) {
        File::removeDir(dir);
    }
}

struct SequencedLogEntry {
    uint64_t seqNum = 0;
    DataPtr data;

    bool operator>(const SequencedLogEntry& other) const {
        return seqNum > other.seqNum;
    }
};

class BaselineLogOutput {
 public:
    BaselineLogOutput() {
        writer_.withLock([](auto& w) {
            w = std::make_unique<Writer>();
        });
    }

    bool open(std::string_view path) {
        return writer_.withLock([&](auto& w) {
            return w->open(path);
        });
    }

    void write(std::string_view str) noexcept {
        writer_.withLock([&](auto& w) {
            w->write(str);
        });
    }

    void close() {
        writer_.withLock([](auto& w) {
            if (w) {
                w->close();
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
                w.reset();
            }
        });
    }

 private:
    Synchronized<std::unique_ptr<Writer>> writer_;
};

class UnorderedThreadLocalLogOutput {
    static constexpr uint64_t kBufferCapacity = 8 * 1024;

    struct ThreadBuffer {
        Data buffer;
        UnorderedThreadLocalLogOutput* owner = nullptr;
        std::shared_ptr<std::atomic<bool>> alive;

        ThreadBuffer() = default;

        ~ThreadBuffer() {
            flush();
        }

        void append(std::string_view str) {
            buffer.append(str);
            if (buffer.length() >= kBufferCapacity) {
                flush();
            }
        }

        void flush() {
            if (!buffer.empty() && owner && alive && alive->load(std::memory_order_acquire)) {
                owner->submitBuffer(buffer.detachData());
            } else {
                buffer.reset();
            }
        }
    };

 public:
    UnorderedThreadLocalLogOutput() : alive_(std::make_shared<std::atomic<bool>>(true)) {
        writer_.withLock([](auto& w) {
            w = std::make_unique<Writer>();
        });
    }

    ~UnorderedThreadLocalLogOutput() {
        alive_->store(false, std::memory_order_release);
    }

    bool open(std::string_view path) {
        return writer_.withLock([&](auto& w) {
            return w->open(path);
        });
    }

    void write(std::string_view str) noexcept {
        auto& buf = getThreadBuffer();
        buf.append(str);
    }

    void flush() noexcept {
        auto& buf = getThreadBuffer();
        buf.flush();
    }

    void close() {
        alive_->store(false, std::memory_order_release);
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        writer_.withLock([](auto& w) {
            if (w) {
                w->close();
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
                w.reset();
            }
        });
    }

 private:
    ThreadBuffer& getThreadBuffer() {
        thread_local ThreadBuffer tls;
        tls.owner = this;
        tls.alive = alive_;
        return tls;
    }

    void submitBuffer(DataPtr data) {
        writer_.withLock([&](auto& w) {
            w->write(std::move(data));
        });
    }

    std::shared_ptr<std::atomic<bool>> alive_;
    Synchronized<std::unique_ptr<Writer>> writer_;
};

class OrderedThreadLocalLogOutput {
    static constexpr uint64_t kBufferCapacity = 8 * 1024;
    static constexpr uint64_t kSortWindowSize = 256;
    static constexpr auto kFlushInterval = std::chrono::milliseconds(10);

    struct SequencedEntry {
        uint64_t seqNum;
        Data data;
    };

    struct ThreadBuffer {
        std::vector<SequencedEntry> entries;
        OrderedThreadLocalLogOutput* owner = nullptr;
        std::shared_ptr<std::atomic<bool>> alive;

        ThreadBuffer() {
            entries.reserve(128);
        }

        ~ThreadBuffer() {
            flush();
        }

        void append(uint64_t seq, std::string_view str) {
            entries.push_back(SequencedEntry{seq, Data(str)});
            uint64_t totalSize = 0;
            for (const auto& e : entries) {
                totalSize += e.data.length();
            }
            if (totalSize >= kBufferCapacity) {
                flush();
            }
        }

        void flush() {
            if (!entries.empty() && owner && alive && alive->load(std::memory_order_acquire)) {
                owner->submitEntries(std::move(entries));
                entries.clear();
                entries.reserve(128);
            } else {
                entries.clear();
            }
        }
    };

 public:
    OrderedThreadLocalLogOutput()
        : alive_(std::make_shared<std::atomic<bool>>(true)),
          globalSeq_(0),
          sortThreadRunning_(true) {
        writer_.withLock([](auto& w) {
            w = std::make_unique<Writer>();
        });
        sortThread_ = std::thread(&OrderedThreadLocalLogOutput::sortAndWriteLoop, this);
    }

    ~OrderedThreadLocalLogOutput() {
        alive_->store(false, std::memory_order_release);
        stopSortThread();
    }

    bool open(std::string_view path) {
        return writer_.withLock([&](auto& w) {
            return w->open(path);
        });
    }

    void write(std::string_view str) noexcept {
        uint64_t seq = globalSeq_.fetch_add(1, std::memory_order_relaxed);
        auto& buf = getThreadBuffer();
        buf.append(seq, str);
    }

    void flush() noexcept {
        auto& buf = getThreadBuffer();
        buf.flush();
        {
            std::lock_guard<std::mutex> lock(sortMutex_);
            sortCond_.notify_one();
        }
    }

    void close() {
        alive_->store(false, std::memory_order_release);
        stopSortThread();
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        writer_.withLock([](auto& w) {
            if (w) {
                w->close();
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
                w.reset();
            }
        });
    }

 private:
    ThreadBuffer& getThreadBuffer() {
        thread_local ThreadBuffer tls;
        tls.owner = this;
        tls.alive = alive_;
        return tls;
    }

    void submitEntries(std::vector<SequencedEntry> entries) {
        {
            std::lock_guard<std::mutex> lock(sortMutex_);
            for (auto& entry : entries) {
                pendingEntries_.push_back(std::move(entry));
            }
        }
        sortCond_.notify_one();
    }

    void stopSortThread() {
        {
            std::lock_guard<std::mutex> lock(sortMutex_);
            sortThreadRunning_ = false;
        }
        sortCond_.notify_one();
        if (sortThread_.joinable()) {
            sortThread_.join();
        }
    }

    void sortAndWriteLoop() {
        while (true) {
            std::vector<SequencedEntry> batch;
            {
                std::unique_lock<std::mutex> lock(sortMutex_);
                sortCond_.wait_for(lock, kFlushInterval, [this] {
                    return !sortThreadRunning_ || pendingEntries_.size() >= kSortWindowSize;
                });

                if (!sortThreadRunning_ && pendingEntries_.empty()) {
                    break;
                }

                batch = std::move(pendingEntries_);
                pendingEntries_.clear();
                pendingEntries_.reserve(256);
            }

            if (batch.empty()) {
                continue;
            }

            std::ranges::sort(batch, [](const SequencedEntry& a, const SequencedEntry& b) {
                return a.seqNum < b.seqNum;
            });

            uint64_t totalSize = 0;
            for (const auto& entry : batch) {
                totalSize += entry.data.length();
            }

            Data merged(totalSize);
            for (const auto& entry : batch) {
                merged.append(entry.data);
            }

            writer_.withLock([&](auto& w) {
                if (w) {
                    w->write(merged.detachData());
                }
            });
        }
    }

    std::shared_ptr<std::atomic<bool>> alive_;
    std::atomic<uint64_t> globalSeq_;
    Synchronized<std::unique_ptr<Writer>> writer_;

    std::thread sortThread_;
    std::mutex sortMutex_;
    std::condition_variable sortCond_;
    bool sortThreadRunning_;
    std::vector<SequencedEntry> pendingEntries_;
};

struct BenchResult {
    std::string name;
    int32_t threadCount;
    int32_t opsPerThread;
    double totalTimeMs;
    double throughputOpsPerSec;
    double avgLatencyNs;
    double p99LatencyNs;
};

static void printResult(const BenchResult& r) {
    std::print("  [{:<35}] threads={:<2} ops/thread={:<7} | total={:.2f}ms | "
               "throughput={:.0f} ops/s | avg_lat={:.0f}ns | p99_lat={:.0f}ns\n",
               r.name, r.threadCount, r.opsPerThread, r.totalTimeMs, r.throughputOpsPerSec,
               r.avgLatencyNs, r.p99LatencyNs);
}

static double percentile(std::vector<int64_t>& sorted, double p) {
    if (sorted.empty()) {
        return 0.0;
    }
    auto idx = static_cast<size_t>(p * static_cast<double>(sorted.size() - 1));
    return static_cast<double>(sorted[idx]);
}

template <typename LogOutputT>
BenchResult runBenchmark(const std::string& name,
                         LogOutputT& output,
                         int32_t threadCount,
                         int32_t opsPerThread) {
    auto tc = static_cast<size_t>(threadCount);
    auto opc = static_cast<size_t>(opsPerThread);
    std::latch startLatch(static_cast<std::ptrdiff_t>(tc + 1));
    std::vector<std::vector<int64_t>> allLatencies(tc);

    std::vector<std::thread> threads;
    threads.reserve(tc);

    for (size_t t = 0; t < tc; ++t) {
        threads.emplace_back([&, t]() {
            auto& latencies = allLatencies[t];
            latencies.reserve(opc);

            startLatch.arrive_and_wait();

            for (size_t i = 0; i < opc; ++i) {
                auto line = genLogLine(static_cast<int32_t>(t), static_cast<int32_t>(i));
                auto start = steady_clock::now();
                output.write(line);
                auto end = steady_clock::now();
                latencies.push_back(duration_cast<nanoseconds>(end - start).count());
            }

            if constexpr (requires { output.flush(); }) {
                output.flush();
            }
        });
    }

    auto wallStart = steady_clock::now();
    startLatch.arrive_and_wait();

    for (auto& t : threads) {
        t.join();
    }
    auto wallEnd = steady_clock::now();

    std::vector<int64_t> merged;
    merged.reserve(tc * opc);
    for (auto& v : allLatencies) {
        merged.insert(merged.end(), v.begin(), v.end());
    }
    std::ranges::sort(merged);

    double totalMs = duration_cast<microseconds>(wallEnd - wallStart).count() / 1000.0;
    int64_t totalOps = static_cast<int64_t>(threadCount) * opsPerThread;
    double throughput = static_cast<double>(totalOps) / (totalMs / 1000.0);
    double avgNs =
        merged.empty()
            ? 0.0
            : static_cast<double>(std::accumulate(merged.begin(), merged.end(), int64_t{0})) /
                  static_cast<double>(merged.size());
    double p99Ns = percentile(merged, 0.99);

    return BenchResult{
        .name = name,
        .threadCount = threadCount,
        .opsPerThread = opsPerThread,
        .totalTimeMs = totalMs,
        .throughputOpsPerSec = throughput,
        .avgLatencyNs = avgNs,
        .p99LatencyNs = p99Ns,
    };
}

}  // anonymous namespace

class OrderedLogOutputBenchmark : public ::testing::Test {
 protected:
    void SetUp() override {
        cleanBenchDir();
    }

    void TearDown() override {
        std::this_thread::sleep_for(milliseconds(500));
        cleanBenchDir();
    }
};

TEST_F(OrderedLogOutputBenchmark, SingleThread_ThreeWayCompare) {
    constexpr int32_t kOps = 50000;

    std::print("\n=== Single Thread: 3-Way Compare (ops={}) ===\n", kOps);

    {
        BaselineLogOutput output;
        output.open(benchLogDir() + "/baseline_1t.log");
        auto r = runBenchmark("Baseline(mutex)", output, 1, kOps);
        printResult(r);
        output.close();
    }
    std::this_thread::sleep_for(milliseconds(100));
    {
        UnorderedThreadLocalLogOutput output;
        output.open(benchLogDir() + "/unordered_1t.log");
        auto r = runBenchmark("ThreadLocal(unordered)", output, 1, kOps);
        printResult(r);
        output.close();
    }
    std::this_thread::sleep_for(milliseconds(100));
    {
        OrderedThreadLocalLogOutput output;
        output.open(benchLogDir() + "/ordered_1t.log");
        auto r = runBenchmark("ThreadLocal(ordered+seq)", output, 1, kOps);
        printResult(r);
        output.close();
    }
}

TEST_F(OrderedLogOutputBenchmark, MultiThread_4_ThreeWayCompare) {
    constexpr int32_t kThreads = 4;
    constexpr int32_t kOpsPerThread = 50000;

    std::print("\n=== 4 Threads: 3-Way Compare (ops/thread={}) ===\n", kOpsPerThread);

    {
        BaselineLogOutput output;
        output.open(benchLogDir() + "/baseline_4t.log");
        auto r = runBenchmark("Baseline(mutex)", output, kThreads, kOpsPerThread);
        printResult(r);
        output.close();
    }
    std::this_thread::sleep_for(milliseconds(100));
    {
        UnorderedThreadLocalLogOutput output;
        output.open(benchLogDir() + "/unordered_4t.log");
        auto r = runBenchmark("ThreadLocal(unordered)", output, kThreads, kOpsPerThread);
        printResult(r);
        output.close();
    }
    std::this_thread::sleep_for(milliseconds(100));
    {
        OrderedThreadLocalLogOutput output;
        output.open(benchLogDir() + "/ordered_4t.log");
        auto r = runBenchmark("ThreadLocal(ordered+seq)", output, kThreads, kOpsPerThread);
        printResult(r);
        output.close();
    }
}

TEST_F(OrderedLogOutputBenchmark, MultiThread_8_ThreeWayCompare) {
    constexpr int32_t kThreads = 8;
    constexpr int32_t kOpsPerThread = 50000;

    std::print("\n=== 8 Threads: 3-Way Compare (ops/thread={}) ===\n", kOpsPerThread);

    {
        BaselineLogOutput output;
        output.open(benchLogDir() + "/baseline_8t.log");
        auto r = runBenchmark("Baseline(mutex)", output, kThreads, kOpsPerThread);
        printResult(r);
        output.close();
    }
    std::this_thread::sleep_for(milliseconds(100));
    {
        UnorderedThreadLocalLogOutput output;
        output.open(benchLogDir() + "/unordered_8t.log");
        auto r = runBenchmark("ThreadLocal(unordered)", output, kThreads, kOpsPerThread);
        printResult(r);
        output.close();
    }
    std::this_thread::sleep_for(milliseconds(100));
    {
        OrderedThreadLocalLogOutput output;
        output.open(benchLogDir() + "/ordered_8t.log");
        auto r = runBenchmark("ThreadLocal(ordered+seq)", output, kThreads, kOpsPerThread);
        printResult(r);
        output.close();
    }
}

TEST_F(OrderedLogOutputBenchmark, Summary_ThreeWay) {
    constexpr int32_t kOps = 50000;
    constexpr std::array<int32_t, 4> kThreadCounts = {1, 2, 4, 8};

    std::string separator(120, '=');
    std::print("\n{}\n", separator);
    std::print("{:^120}\n", "SUMMARY: Baseline vs Unordered TLS vs Ordered TLS (seq+sort)");
    std::print("{}\n", separator);

    std::vector<BenchResult> baselineResults;
    std::vector<BenchResult> unorderedResults;
    std::vector<BenchResult> orderedResults;

    for (auto tc : kThreadCounts) {
        {
            BaselineLogOutput output;
            output.open(benchLogDir() + std::format("/summary_baseline_t{}.log", tc));
            baselineResults.push_back(runBenchmark("Baseline", output, tc, kOps));
            output.close();
        }
        std::this_thread::sleep_for(milliseconds(100));
        {
            UnorderedThreadLocalLogOutput output;
            output.open(benchLogDir() + std::format("/summary_unordered_t{}.log", tc));
            unorderedResults.push_back(runBenchmark("Unordered", output, tc, kOps));
            output.close();
        }
        std::this_thread::sleep_for(milliseconds(100));
        {
            OrderedThreadLocalLogOutput output;
            output.open(benchLogDir() + std::format("/summary_ordered_t{}.log", tc));
            orderedResults.push_back(runBenchmark("Ordered", output, tc, kOps));
            output.close();
        }
        std::this_thread::sleep_for(milliseconds(100));
    }

    std::print("\n{:<8} | {:<16} {:<16} {:<16} | {:<14} {:<14} {:<14} | {:<8} {:<8}\n", "Threads",
               "Baseline(ops/s)", "Unord(ops/s)", "Ordered(ops/s)", "Base avg(ns)", "Unord avg(ns)",
               "Ord avg(ns)", "Unord/B", "Ord/B");
    std::string dashLine(140, '-');
    std::print("{}\n", dashLine);

    for (size_t i = 0; i < kThreadCounts.size(); ++i) {
        double speedupUnordered =
            unorderedResults[i].throughputOpsPerSec / baselineResults[i].throughputOpsPerSec;
        double speedupOrdered =
            orderedResults[i].throughputOpsPerSec / baselineResults[i].throughputOpsPerSec;
        std::print("{:<8} | {:<16.0f} {:<16.0f} {:<16.0f} | {:<14.0f} {:<14.0f} {:<14.0f} | "
                   "{:<8.2f}x {:<8.2f}x\n",
                   kThreadCounts[i], baselineResults[i].throughputOpsPerSec,
                   unorderedResults[i].throughputOpsPerSec, orderedResults[i].throughputOpsPerSec,
                   baselineResults[i].avgLatencyNs, unorderedResults[i].avgLatencyNs,
                   orderedResults[i].avgLatencyNs, speedupUnordered, speedupOrdered);
    }

    std::print("\n");

    std::print("{:<8} | {:<16} {:<16} {:<16}\n", "Threads", "Base P99(ns)", "Unord P99(ns)",
               "Ord P99(ns)");
    std::print("{}\n", std::string(80, '-'));
    for (size_t i = 0; i < kThreadCounts.size(); ++i) {
        std::print("{:<8} | {:<16.0f} {:<16.0f} {:<16.0f}\n", kThreadCounts[i],
                   baselineResults[i].p99LatencyNs, unorderedResults[i].p99LatencyNs,
                   orderedResults[i].p99LatencyNs);
    }
    std::print("\n");
}
