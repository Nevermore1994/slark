//
// Created by Nevermore on 2022/5/11.
// slark ReadHandler
// Copyright (c) 2022 Nevermore All rights reserved.
//
#include "ReadHandler.hpp"
#include "Log.hpp"
#include "FileUtility.h"
#include "Assert.hpp"

namespace slark {

ReadHandler::ReadHandler()
    : worker_("ReadThread", &ReadHandler::process, this)
    , file_(nullptr){
}

ReadHandler::~ReadHandler() {
    worker_.stop();
    if (file_->isOpen()) {
        file_->close();
    }
}

bool ReadHandler::open(const std::string& path) noexcept {
    std::unique_lock<std::mutex> lock(mutex_);
    path_ = path;
    file_ = std::make_unique<FileUtil::File>(path);
    if (file_->open()) {
        file_->seek(0);
        worker_.start();
    } else {
        LogE("open file failed. %s", path_.c_str());
    }
    return file_->isOpen();
}

void ReadHandler::resume() noexcept {
    if(!file_ || !file_->isOpen()) {
        LogE("file not open.");
        return;
    }
    worker_.resume();
}

void ReadHandler::pause() noexcept {
    if(!file_ || !file_->isOpen()) {
        LogE("file not open.");
        return;
    }
    worker_.pause();
}

void ReadHandler::close() noexcept {
    std::unique_lock<std::mutex> lock(mutex_);
    worker_.pause();
    if (file_ && file_->isOpen()) {
        file_->close();
        file_.reset();
    }
}

void ReadHandler::seek(uint64_t pos) {
    if(!file_ || !file_->isOpen()) {
        LogE("file not open.");
        return;
    }
    std::unique_lock<std::mutex> lock(mutex_);
    seekPos_ = static_cast<int64_t>(pos);
}

void ReadHandler::process() {
    if (!file_ || !file_->isOpen()) {
        return;
    }
    if (seekPos_ != kInvalid) {
        std::unique_lock<std::mutex> lock(mutex_);
        file_->seek(seekPos_);
        seekPos_ = kInvalid;
    }

    auto offset = file_->tell();
    std::unique_ptr<Data> data;
    {
        std::unique_lock<std::mutex> lock(mutex_);
        data = std::make_unique<Data>(IO::kReadDefaultSize);
        file_->read(*data);
    }

    if (handler_ && data) {
        handler_(std::move(data), static_cast<int64_t>(offset), state());
    }
}

IOState ReadHandler::state() const noexcept {
    if (!file_) {
        return IOState::Error;
    }

    auto offset = static_cast<uint64_t>(file_->tell());
    if (file_->readOver() || offset == file_->fileSize()) {
        return IOState::EndOfFile;
    } else if (file_->isFailed() || !file_->isOpen()) {
        return IOState::Error;
    }
    return IOState::Normal;
}

}//end namespace slark
