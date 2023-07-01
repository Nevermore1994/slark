//
// Created by Nevermore on 2022/5/11.
// slark FileHandler
// Copyright (c) 2022 Nevermore All rights reserved.
//
#include "FileHandler.hpp"
#include "Log.hpp"
#include "FileUtility.h"
#include "Assert.h"

namespace slark {

FileHandler::FileHandler()
    : worker_("IOThread", &FileHandler::process, this)
    , file_(nullptr){
}

FileHandler::~FileHandler() {
    worker_.stop();
    if (file_->isOpen()) {
        file_->close();
    }
}

bool FileHandler::open(std::string path) {
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

void FileHandler::resume() {
    worker_.resume();
}

void FileHandler::pause() {
    worker_.pause();
}

void FileHandler::close() {
    std::unique_lock<std::mutex> lock(mutex_);
    worker_.pause();
    if (file_->isOpen()) {
        file_->close();
    }
}

void FileHandler::seek(uint64_t pos) {
    std::unique_lock<std::mutex> lock(mutex_);
    seekPos_ = pos;
}

void FileHandler::write(std::unique_ptr<Data> data) {
    std::unique_lock<std::mutex> lock(mutex_);
    writeData_.push_back(std::move(data));
}

void FileHandler::write(uint8_t* data, uint64_t length) {
    SAssert(data != nullptr, "error !!!, write null data.");
    auto p = std::make_unique<Data>(data, length);
    std::unique_lock<std::mutex> lock(mutex_);
    writeData_.push_back(std::move(p));
}

void FileHandler::process() {
    if (seekPos_ != kInvalid) {
        std::unique_lock<std::mutex> lock(mutex_);
        file_->seek(seekPos_);
        seekPos_ = kInvalid;
    }
    {
        std::unique_lock<std::mutex> lock(mutex_);
        for (const auto& data : writeData_) {
            file_->write(*data);
        }
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

IOState FileHandler::state() noexcept {
    auto offset = file_->tell();
    if (file_->readOver() || offset == file_->fileSize()) {
        return IOState::EndOfFile;
    } else if (file_->isFailed()) {
        return IOState::Error;
    }
    return IOState::Normal;
}

}//end namespace slark
