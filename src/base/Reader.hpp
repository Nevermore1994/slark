//
// Created by Nevermore on 2022/5/11.
// slark ReadHandler
// Copyright (c) 2022 Nevermore All rights reserved.
//
#pragma once

#include "IOHandler.h"
#include "Thread.h"
#include "File.h"
#include "Synchronized.hpp"
#include <optional>
#include <chrono>

namespace slark {

constexpr uint64_t kReadDefaultSize = 1024 * 4;
using ReaderDataFunc = std::function<void(DataPtr, int64_t, IOState)>;

struct ReaderSetting {
    uint64_t readBlockSize = kReadDefaultSize;
    std::chrono::milliseconds timeInterval{5};
    ReaderDataFunc callBack;
    
    ReaderSetting() = default;
    
    explicit ReaderSetting(ReaderDataFunc&& func)
        : callBack(std::move(func)) {
        
    }
    
    ReaderSetting(uint64_t readSize, ReaderDataFunc&& func)
        : readBlockSize(readSize)
        , callBack(std::move(func)) {
        
    }
};

class Reader: public IOHandler {
public:
    Reader();
    ~Reader() override;
public:
    bool open(std::string_view path, ReaderSetting&& setting);
    [[nodiscard]] IOState state() noexcept override;
    void close() noexcept override;
    std::string_view path() noexcept override;

    void start() noexcept;
    void resume() noexcept;
    void pause() noexcept;
    void stop() noexcept;

    void seek(uint64_t pos) noexcept;
    int64_t tell() noexcept;
    
    const ReaderSetting& readerSetting() noexcept {
        return setting_;
    }
private:
    void process();
private:
    std::optional<int64_t> seekPos_;
    ReaderSetting setting_;
    Synchronized<std::unique_ptr<FileUtil::ReadFile>, std::shared_mutex> file_;
    Thread worker_;
};

}
