//
// Created by Nevermore on 2023/7/1.
// slark LogOutput
// Copyright (c) 2023 Nevermore All rights reserved.
//
#include "LogOutput.h"

#include "Log.hpp"
#include "Time.hpp"

#include <algorithm>
#include <memory>
#include <ranges>

namespace slark {

static std::string logFileName() {
    using namespace slark::File;
    const std::string logDir = cachePath() + "/logs";
    if (!isDirExist(logDir) && !createDir(logDir)) {
        LogP("create log folder failed.");
        return "";
    }
    return logDir + "/" + Time::localShortTimeStr() + ".log";
}

static constexpr uint32_t kMaxWriteLogCount = 80 * 1024;

LogOutput& LogOutput::shareInstance() {
    static std::unique_ptr<LogOutput> instance;
    static std::once_flag flag;
    std::call_once(flag, [] {
        instance = std::make_unique<LogOutput>();
    });
    return *instance;
}

LogOutput::ThreadBuffer::ThreadBuffer() {
    entries.reserve(128);
}

LogOutput::ThreadBuffer::~ThreadBuffer() {
    flush();
}

void LogOutput::ThreadBuffer::append(uint64_t seq, std::string_view str) {
    bufferedSize += str.size();
    entries.push_back(LogEntry{seq, Data(str)});
    if (bufferedSize >= kBufferCapacity) {
        flush();
    }
}

void LogOutput::ThreadBuffer::flush() {
    if (entries.empty() || !owner || !owner->alive_.load(std::memory_order_acquire)) {
        return;
    }
    owner->submitEntries(std::move(entries));
    entries.clear();
    entries.reserve(128);
    bufferedSize = 0;
}

LogOutput::LogOutput() {
    writer_.withLock([](auto& writer) {
        writer = std::make_unique<Writer>();
        recreateFile(writer);
    });
    sortThread_ = std::thread(&LogOutput::sortAndWriteLoop, this);
}

LogOutput::~LogOutput() {
    alive_.store(false, std::memory_order_release);
    sortThreadRunning_.store(false, std::memory_order_release);
    sortCond_.notify_one();
    if (sortThread_.joinable()) {
        sortThread_.join();
    }
}

void LogOutput::write(std::string_view str) noexcept {
    auto seq = globalSeq_.fetch_add(1, std::memory_order_relaxed);
    getThreadBuffer().append(seq, str);
}

LogOutput::ThreadBuffer& LogOutput::getThreadBuffer() noexcept {
    thread_local ThreadBuffer tls;
    if (tls.owner != this) [[unlikely]] {
        tls.owner = this;
    }
    return tls;
}

void LogOutput::submitEntries(std::vector<LogEntry> entries) {
    entryQueue_.push(std::move(entries));
    sortCond_.notify_one();
}

void LogOutput::sortAndWriteLoop() {
    std::vector<LogEntry> merged;

    while (true) {
        {
            std::unique_lock lock(sortMutex_);
            sortCond_.wait_for(lock, kFlushInterval, [this] {
                return !sortThreadRunning_.load(std::memory_order_acquire) || !entryQueue_.empty();
            });
        }

        auto batches = entryQueue_.tryPopAll();
        if (batches.empty()) {
            if (!sortThreadRunning_.load(std::memory_order_acquire)) {
                break;
            }
            continue;
        }

        // 合并所有批次并按序列号排序
        merged.clear();
        for (auto& batch : batches) {
            merged.insert(merged.end(),
                          std::make_move_iterator(batch.begin()),
                          std::make_move_iterator(batch.end()));
        }
        std::ranges::sort(merged, {}, &LogEntry::seq);

        // 合并为单个 Data 批量写入
        uint64_t totalSize = 0;
        for (const auto& entry : merged) {
            totalSize += entry.data.length();
        }
        Data mergedData(totalSize);
        for (const auto& entry : merged) {
            mergedData.append(entry.data);
        }

        writer_.withLock([&](auto& writer) {
            if (!writer) [[unlikely]] {
                return;
            }
            writer->write(mergedData.detachData());
            if (writer->writeCount() >= kMaxWriteLogCount) {
                recreateFile(writer);
            }
        });
    }
}

void LogOutput::recreateFile(std::unique_ptr<Writer>& writer) noexcept {
    auto path = logFileName();
    if (path.empty()) {
        LogP("log file path is empty");
        return;
    }
    writer->open(path);
}

} // namespace slark
