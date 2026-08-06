//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//
#include "LogOutput.h"

#include <algorithm>
#include <filesystem>
#include <memory>
#include <ranges>

#include "Log.hpp"
#include "LogConfig.h"
#include "Time.hpp"

namespace slark {

static std::string defaultLogDir() {
    using namespace slark::File;
    const std::string logDir = cachePath() + "/logs";
    if (!isDirExist(logDir) && !createDir(logDir)) {
        LogP("create log folder failed.");
        return "";
    }
    return logDir;
}

LogOutput& LogOutput::shareInstance() {
    static LogOutput* instance = new LogOutput();
    return *instance;
}

void outputLog(LogType level, const std::string& log) {
    if (level != LogType::Print && static_cast<int>(level) >= getLogConfig().minLogLevel) {
        LogOutput::shareInstance().write(log);
    }
    if (level == LogType::Record) {
        return;
    }
#if SLAKR_IOS
    printLog(level, log);
#elif SLARK_ANDROID
    printLog(level, log);
#else
    std::print("{}", log);
    fflush(stdout);
#endif
}

void LogOutput::shutdown() noexcept {
    auto& self = shareInstance();
    self.alive_.store(false, std::memory_order_release);
    self.sortThreadRunning_.store(false, std::memory_order_release);
    self.sortCond_.notify_one();
    if (self.sortThread_.joinable()) {
        self.sortThread_.join();
    }
    self.writer_.withLock([](auto& writer) {
        if (writer) {
            writer->close();
        }
    });
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
    if (entries.empty()) {
        return;
    }
    owner->submitEntries(std::move(entries));
    entries.clear();
    entries.reserve(128);
    bufferedSize = 0;
}

LogOutput::LogOutput() {
    pruneOldLogs();
    writer_.withLock([this](auto& writer) {
        writer = std::make_unique<Writer>();
        recreateFile(writer);
    });
    sortThread_ = std::thread(&LogOutput::sortAndWriteLoop, this);
}

void LogOutput::write(std::string_view str) noexcept {
    if (!alive_.load(std::memory_order_acquire)) {
        return;
    }
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
    if (!alive_.load(std::memory_order_acquire)) {
        return;
    }
    {
        // Mutual exclusion with sortAndWriteLoop's predicate check avoids a lost wakeup
        std::lock_guard lock(sortMutex_);
        entryQueue_.push(std::move(entries));
    }
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

        merged.clear();
        for (auto& batch : batches) {
            merged.insert(merged.end(), std::make_move_iterator(batch.begin()),
                          std::make_move_iterator(batch.end()));
        }
        std::ranges::sort(merged, {}, &LogEntry::seq);

        // Merge into a single Data for batched write.
        // Do not use Data(totalSize) + append(): Data(totalSize) sets length_=totalSize and
        // append would trigger reallocation, producing totalSize zeros followed by real content.
        // Use fromString to build an empty Data then append, or copy in one shot.
        uint64_t totalSize = 0;
        for (const auto& entry : merged) {
            totalSize += entry.data.length();
        }
        Data mergedData(totalSize, [&merged](Byte* buffer) -> uint64_t {
            uint64_t offset = 0;
            for (const auto& entry : merged) {
                auto len = entry.data.length();
                std::copy_n(entry.data.data(), len, buffer + offset);
                offset += len;
            }
            return offset;
        });

        writer_.withLock([&](auto& writer) {
            if (!writer) [[unlikely]] {
                return;
            }
            writer->write(mergedData.detachData());
            currentFileWriteSize_ += totalSize;
            rotateIfNeeded(writer);
        });
    }
}

std::string LogOutput::logFilePath() const {
    const auto& config = getLogConfig();
    const std::string dir = config.logDir.empty() ? defaultLogDir() : config.logDir;
    if (dir.empty()) {
        return "";
    }
    const std::string fileName =
        config.logFileName.empty()
            ? (Time::localShortTimeStr() + ".log")
            : (config.logFileName + "_" + Time::localShortTimeStr() + ".log");
    return dir + "/" + fileName;
}

void LogOutput::rotateIfNeeded(std::unique_ptr<Writer>& writer) noexcept {
    const auto& config = getLogConfig();
    if (currentFileWriteSize_ < config.maxLogFileSize) {
        return;
    }

    writer->close();

    const std::string basePath = logFilePath();
    if (basePath.empty()) {
        return;
    }

    try {
        // Rotate: basePath → .1, .1 → .2 ... .(N-1) → .N, then delete .N.
        // Keeps the current file plus .1 ~ .(N-1), N files in total.
        const uint32_t maxNum = config.maxLogFileCount - 1;
        std::filesystem::remove(basePath + "." + std::to_string(maxNum));
        for (uint32_t i = maxNum; i > 0; --i) {
            const std::string oldPath = basePath + "." + std::to_string(i - 1);
            const std::string newPath = basePath + "." + std::to_string(i);
            if (std::filesystem::exists(oldPath)) {
                std::filesystem::rename(oldPath, newPath);
            }
        }
        std::filesystem::rename(basePath, basePath + ".1");
    } catch (...) {
        // Rotation failure must not block log writing
    }

    recreateFile(writer);
}

void LogOutput::recreateFile(std::unique_ptr<Writer>& writer) noexcept {
    currentFileWriteSize_ = 0;
    auto path = logFilePath();
    if (path.empty()) {
        LogP("log file path is empty");
        return;
    }
    if (!std::filesystem::exists(std::filesystem::path(path).parent_path())) {
        std::filesystem::create_directories(std::filesystem::path(path).parent_path());
    }
    if (!writer->open(path)) {
        LogP("open log file failed: {}", path);
        return;
    }
    LogP("slark log file: {}", path);
}

void LogOutput::pruneOldLogs() noexcept {
    const auto& config = getLogConfig();
    const std::string dir = config.logDir.empty() ? defaultLogDir() : config.logDir;
    if (dir.empty()) {
        return;
    }

    const std::string prefix = config.logFileName;

    try {
        for (const auto& entry : std::filesystem::directory_iterator(dir)) {
            if (!entry.is_regular_file()) {
                continue;
            }
            auto stem = entry.path().stem().string();      // e.g. "slark_20260717" or "20260717"
            auto ext = entry.path().extension().string();  // e.g. ".log" or ".1"

            if (ext.size() <= 1 || ext[1] < '0' || ext[1] > '9') {
                continue;
            }

            bool matched = false;
            if (prefix.empty()) {
                // No prefix: default format <time>.log.N
                matched = true;
            } else {
                // With prefix: <prefix>_<time>.log.N
                matched = stem.starts_with(prefix + "_");
            }
            if (!matched) {
                continue;
            }

            auto num = std::stoul(ext.substr(1));
            if (num > config.maxLogFileCount - 1) {
                std::filesystem::remove(entry.path());
            }
        }
    } catch (...) {
        // Filesystem failures must not block logging
    }
}

namespace internal {

[[noreturn]] void logAssertAndTrap(std::string_view file,
                                   int line,
                                   std::string_view func,
                                   std::string_view format,
                                   std::format_args args) {
    auto logStr = std::vformat(format, args);
    auto fullLog =
        std::format("{}{}[{}][Line:{}][Function:{}] {}\n", Time::localTimeStr(),
                    kLogTags[static_cast<size_t>(LogType::Assert)], file, line, func, logStr);
    outputLog(LogType::Assert, fullLog);
    __builtin_trap();
}

}  // namespace internal

}  // namespace slark
