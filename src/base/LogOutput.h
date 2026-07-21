//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//
#pragma once
#include "Writer.h"

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <thread>
#include <vector>

namespace slark {

class LogOutput : public NonCopyMovable {
public:
    static LogOutput& shareInstance();

    LogOutput();
    ~LogOutput() = delete;

    void write(std::string_view str) noexcept;

    /// 关闭日志系统：刷新所有缓存日志后停止排序线程。
    /// shutdown 后可继续调用 write（日志将被丢弃）。
    /// 幂等，可多次调用。
    static void shutdown() noexcept;

private:
    struct LogEntry {
        uint64_t seq = 0;
        Data data;
    };

    static constexpr uint64_t kBufferCapacity = 8 * 1024;
    static constexpr auto kFlushInterval = std::chrono::milliseconds(50);

    struct ThreadBuffer {
        std::vector<LogEntry> entries;
        uint64_t bufferedSize = 0;
        LogOutput* owner = nullptr;

        ThreadBuffer();
        ~ThreadBuffer();

        void append(uint64_t seq, std::string_view str);
        void flush();
    };

    ThreadBuffer& getThreadBuffer() noexcept;
    void submitEntries(std::vector<LogEntry> entries);
    void sortAndWriteLoop();
    void recreateFile(std::unique_ptr<Writer>& writer) noexcept;
    void rotateIfNeeded(std::unique_ptr<Writer>& writer) noexcept;
    void pruneOldLogs() noexcept;
    std::string logFilePath() const;

    std::atomic<bool> alive_{true};
    std::atomic<bool> sortThreadRunning_{true};
    std::atomic<uint64_t> globalSeq_{0};
    uint64_t currentFileWriteSize_ = 0;
    Synchronized<std::unique_ptr<Writer>> writer_;

    MPSCQueue<std::vector<LogEntry>> entryQueue_;
    std::mutex sortMutex_;
    std::condition_variable sortCond_;
    std::thread sortThread_;
};

} // namespace slark
