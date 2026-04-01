//
// Created by Nevermore on 2022/5/11.
// slark ReadHandler
// Copyright (c) 2022 Nevermore All rights reserved.
//
#pragma once

#include "File.h"
#include "IReader.h"
#include "Synchronized.hpp"
#include "Thread.h"

#include <optional>

namespace slark {

class Reader : public IReader {
public:
    Reader();
    ~Reader() override;

    bool open(ReaderTaskPtr) noexcept override;
    IOState state() noexcept override;
    void reset() noexcept override;
    void close() noexcept override;
    void start() noexcept override;
    void pause() noexcept override;
    bool isRunning() noexcept override;
    bool isCompleted() noexcept override;
    void updateReadRange(Range range) noexcept override;
    void seek(uint64_t pos) noexcept override;
    uint64_t size() noexcept override;

    int64_t tell() noexcept;
    std::string path() noexcept;

private:
    void process() noexcept;
    void doSeek() noexcept;
    void closeFile() noexcept;

    std::atomic_bool isReadCompleted_ = false;
    std::mutex seekMutex_;
    std::optional<int64_t> seekPos_;
    Synchronized<std::unique_ptr<File::ReadFile>, std::shared_mutex> file_;
    TimerId readTimerId_;
    std::shared_ptr<Thread> worker_;
};

} // namespace slark
