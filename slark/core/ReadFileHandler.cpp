//
// Created by Nevermore on 2022/5/11.
// slark ReadFileHandler
// Copyright (c) 2022 Nevermore All rights reserved.
//
#include "ReadFileHandler.hpp"
#include "Log.hpp"
#include "FileUtility.h"
#include "Assert.h"

namespace slark {

ReadFileHandler::ReadFileHandler()
    : worker_("IOThread", &ReadFileHandler::process, this)
    , file_(nullptr){
}

ReadFileHandler::~ReadFileHandler() {
    worker_.stop();
    if (file_->isOpen()) {
        file_->close();
    }
}

bool ReadFileHandler::open(std::string path) {
    std::unique_lock<std::mutex> lock(mutex_);
    path_ = path;
    file_ = std::make_unique<FileUtil::File>(path);
    if (file_->open()) {
        file_->seek(0);
        worker_.start();
    } else {
        LogE("open file failed. %s", path_.c_str());
    }
    return file_->open();
}

void ReadFileHandler::resume() {
    worker_.resume();
}

void ReadFileHandler::pause() {
    worker_.pause();
}

void ReadFileHandler::close() {
    std::unique_lock<std::mutex> lock(mutex_);
    worker_.pause();
    if (file_->isOpen()) {
        file_->close();
    }
}

void ReadFileHandler::seek(uint64_t pos) {
    std::unique_lock<std::mutex> lock(mutex_);
    seekPos_ = static_cast<int64_t>(pos);
}

void ReadFileHandler::process() {
    if (seekPos_ != kInvalid) {
        std::unique_lock<std::mutex> lock(mutex_);
        file_->seek(seekPos_);
        seekPos_ = kInvalid;
    }

    auto offset = file_->tell();
    std::unique_ptr<Data> data;
    {
        std::unique_lock<std::mutex> lock(mutex_);
        data = std::make_unique<Data>(kDefaultSize);
        file_->read(*data);
    }

    if (handler_ && data) {
        handler_(std::move(data), static_cast<int64_t>(offset), state());
    }
}

IOState ReadFileHandler::state() noexcept {
    auto offset = static_cast<uint64_t>(file_->tell());
    if (file_->readOver() || offset == file_->fileSize()) {
        return IOState::EndOfFile;
    } else if (file_->isFailed()) {
        return IOState::Error;
    }
    return IOState::Normal;
}

}//end namespace slark
