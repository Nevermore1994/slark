//
// Created by Nevermore on 2022/5/11.
// slark FileHandler
// Copyright (c) 2022 Nevermore All rights reserved.
//
#include "FileHandler.hpp"
#include "Log.hpp"
#include "FileUtility.hpp"
#include <cassert>

using namespace slark;
using namespace slark;

FileHandler::FileHandler()
    : file_(nullptr)
    , worker_("IOThread", &FileHandler::process, this){
}

FileHandler::~FileHandler() {
    worker_.stop();
    if(file_->stream().is_open()) {
        file_->close();
    }
}

bool FileHandler::open(std::string path) {
    std::unique_lock<std::mutex> lock(mutex_);
    path_ = path;
    file_ = std::make_unique<FileUtil::File>(path, std::ios_base::out | std::ios_base::in | std::ios_base::binary);
    if(file_->open()) {
        file_->stream().seekg(std::ios_base::beg);
        worker_.start();
    } else {
        loge("open file failed. %s", path_.c_str());
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
    if(file_->isOpen()) {
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

void FileHandler::write(const char *data, uint64_t length) {
    if(data == nullptr){
        assert("error !!!, write null data.");
    }
    auto p = std::make_unique<Data>(data, length);
    std::unique_lock<std::mutex> lock(mutex_);
    writeData_.push_back(std::move(p));
}

void FileHandler::process() {
    if(seekPos_ != kInvalid) {
        std::unique_lock<std::mutex> lock(mutex_);
        file_->stream().seekg(seekPos_);
        seekPos_ = kInvalid;
    }
    {
        std::unique_lock<std::mutex> lock(mutex_);
        for(const auto& data: writeData_) {
            file_->stream().write(data->data, data->length);
        }
    }
    
    auto offset = file_->stream().tellg();
    std::unique_ptr<Data> data;
    {
        std::unique_lock<std::mutex> lock(mutex_);
        data = std::make_unique<Data>(kDefaultSize);
        file_->stream().read(data->data, data->capacity);
        data->length = file_->stream().gcount();
    }
    
    if(handler_ && data){
        handler_(std::move(data), static_cast<int64_t>(offset), state());
    }
}

IOState FileHandler::state() noexcept {
    auto offset = file_->stream().tellg();
    if(file_->stream().eof() || offset == file_->fileSize()){
        return IOState::Eof;
    } else if(file_->stream().fail()){
        return IOState::Failed;
    } else if(file_->stream().bad()){
        return IOState::Error;
    }
    return IOState::Normal;
}


