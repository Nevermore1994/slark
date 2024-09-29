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

struct IOData {
    DataPtr data = nullptr;
    int64_t offset = 0;
    
    IOData() = default;
    IOData(uint64_t size)
        : data(std::make_unique<Data>(size)) {
        
    }
    IOData(IOData&& d)
        : data(std::move(d.data))
        , offset(d.offset) {
        
    }
    
    IOData& operator=(IOData&& d) {
        data = std::move(d.data);
        offset = d.offset;
        return *this;
    }
};

using ReaderDataFunc = std::function<void(IOData, IOState)>;

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
     
    [[nodiscard]] bool isRunning() noexcept {
        return worker_.isRunning();
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
