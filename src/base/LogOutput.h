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
    ~LogOutput();

    void write(std::string_view str) noexcept;

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
    static void recreateFile(std::unique_ptr<Writer>& writer) noexcept;

    std::atomic<bool> alive_{true};
    std::atomic<bool> sortThreadRunning_{true};
    std::atomic<uint64_t> globalSeq_{0};
    Synchronized<std::unique_ptr<Writer>> writer_;

    MPSCQueue<std::vector<LogEntry>> entryQueue_;
    std::mutex sortMutex_;
    std::condition_variable sortCond_;
    std::thread sortThread_;
};

} // namespace slark
