//
// Created by Nevermore on 2022/5/11.
// slark ReadHandler
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

class ReadHandler: public IOHandler {
public:
    ReadHandler();
    ~ReadHandler() override;
public:
    [[nodiscard]] IOState state() const noexcept override;
    bool open(const std::string& path) noexcept override;
    void resume() noexcept override;
    void pause() noexcept override;
    void close() noexcept override;

    void seek(uint64_t pos);
    [[nodiscard]] inline int64_t tell() const {
        if (file_) {
            return file_->tell();
        }
        return 0;
    }
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
private:
    int64_t seekPos_ = kInvalid;
    std::mutex mutex_;
    std::string path_;
    slark::Thread worker_;
    std::unique_ptr<slark::FileUtil::ReadFile> file_;
    IOHandlerCallBack handler_;
};

}


