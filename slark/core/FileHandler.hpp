//
// Created by Nevermore on 2022/5/11.
// slark FileHandler
// Copyright (c) 2022 Nevermore All rights reserved.
//
#pragma once

#include "IOHandler.h"
#include "Base.h"
#include "Thread.h"
#include "File.h"
#include <list>
#include <mutex>

namespace slark {

constexpr uint32_t kDefaultSize = 1024 * 5;

class FileHandler: public IOHandler {
public:
    FileHandler();
    ~FileHandler() override;
public:
    bool open(std::string path) override;
    void resume() override;
    void pause() override;
    void close() override;

    void seek(uint64_t pos);
    void write(std::unique_ptr<Data> data);
    void write(uint8_t* data, uint64_t length);

public:
    inline std::string_view path() const noexcept override {
        return path_;
    }

    inline void setCallBack(IOHandlerCallBack callBack) noexcept override {
        std::unique_lock<std::mutex> lock(mutex_);
        handler_ = std::move(callBack);
    }
private:
    void process();
    IOState state() noexcept;
private:
    int64_t seekPos_ = kInvalid;
    std::mutex mutex_;
    std::string path_;
    slark::Thread worker_;
    std::unique_ptr<slark::FileUtil::File> file_;
    std::list<std::unique_ptr<Data>> writeData_;
    IOHandlerCallBack handler_;
};

}


