//
// Created by Nevermore on 2024/3/19.
// slark ReadHandler
// Copyright (c) 2024 Nevermore All rights reserved.
//
#include "Reader.h"
#include "Log.hpp"
#include "FileUtil.h"
#include "Util.hpp"
#include <string>

namespace slark {

static const std::string kReaderPrefixName = "Reader_";

Reader::Reader()
    : worker_(Util::genRandomName(kReaderPrefixName), &Reader::process, this) {
    type_ = ReaderType::Local;
}

Reader::~Reader() {
    worker_.pause();
    file_.withWriteLock([](auto& file){
        if (file) {
            file->close();
            LogI("{} reader closed", file->path());
        }
        file.reset();
    });
    {
        std::lock_guard<std::mutex> lock(taskMutex_);
        task_.reset();
    }
    worker_.stop();
}

bool Reader::open(ReaderTaskPtr ptr) noexcept {
    bool isSuccess = false;
    worker_.setInterval(ptr->timeInterval);
    file_.withWriteLock([&](auto& file){
        file = std::make_unique<FileUtil::ReadFile>(std::string(ptr->path));
        isSuccess = file->open();
    });
    {
        std::lock_guard<std::mutex> lock(taskMutex_);
        task_ = std::move(ptr);
    }
    return isSuccess;
}

bool Reader::isCompleted() noexcept {
    bool hasSeek = false;
    {
        std::lock_guard<std::mutex> lock(seekMutex_);
        hasSeek = seekPos_.has_value();
    }
    return isReadCompleted_ && !hasSeek;
}

bool Reader::isRunning() noexcept {
    return worker_.isRunning();
}

IOState Reader::state() noexcept {
    IOState state = IOState::Normal;
    uint64_t tell = 0;
    file_.withReadLock([&state, &tell, this](auto& file){
        if (!file || worker_.isExit()) {
            state = IOState::Closed;
        } else if (!worker_.isRunning()) {
            state = IOState::Pause;
        } else if (file->isFailed()) {
            state = IOState::Error;
        } else if(file->readOver()) {
            state = IOState::EndOfFile;
        }
        if (file) {
            tell = file->tell();
        }
    });
    do {
        std::lock_guard<std::mutex> lock(taskMutex_);
        if (!task_) {
            break;
        }
        const auto& readRange = task_->range;
        if(readRange.isValid() && tell >= readRange.end()) {
            state = IOState::EndOfFile;
        }
    } while(false);
    return state;
}

void Reader::start() noexcept {
    worker_.start();
}

void Reader::pause() noexcept {
    worker_.pause();
}

void Reader::reset() noexcept {
    worker_.pause();
    file_.withWriteLock([](auto& file){
        if (file) {
            file->close();
            LogI("{} reader closed", file->path());
        }
        file.reset();
    });
    {
        std::lock_guard<std::mutex> lock(taskMutex_);
        task_.reset();
    }
}

void Reader::close() noexcept {
    reset();
    worker_.stop();
}

std::string Reader::path() noexcept {
    std::string path;
    file_.withReadLock([&](auto& file){
        if (file) {
            path = file->path();
        }
    });
    return path;
}

void Reader::seek(uint64_t pos) noexcept {
    if (worker_.isExit()) {
        LogE("Reader is exit.");
        return;
    }
    
    std::lock_guard<std::mutex> lock(seekMutex_);
    seekPos_ = static_cast<int64_t>(pos);
    isReadCompleted_ = false;
}

void Reader::doSeek() noexcept {
    std::lock_guard<std::mutex> lock(seekMutex_);
    if (seekPos_.has_value()) {
        file_.withReadLock([&](auto& file){
            if (!file) {
                return;
            }
            file->seek(seekPos_.value());
        });
        seekPos_.reset();
    }
}

void Reader::process() noexcept {
    doSeek();
    auto nowState = state();
    if (nowState == IOState::Error) {
        worker_.pause();
        LogE("Reader is in error state.");
        return;
    } else if (nowState == IOState::EndOfFile) {
        worker_.pause();
        isReadCompleted_ = true;
        LogI("read data completed.");
        return;
    }

    uint64_t readBlockSize = kReadDefaultSize;
    Range readRange;
    {
        std::lock_guard<std::mutex> lock(taskMutex_);
        if (!task_) {
            return;
        }
        readBlockSize = task_->readBlockSize;
        readRange = task_->range;
    }
    DataPacket data(readBlockSize);
    file_.withReadLock([&](auto& file){
        if (!file) {
            return;
        }

        auto tell = file->tell();
        auto readSize = data.data->capacity;
        if (readRange.isValid() && readSize >= (readRange.end() - tell)) {
            readSize = readRange.end() - tell;
        }
        data.offset = static_cast<int64_t>(tell);
        file->read(*data.data, readSize);
    });
     
    nowState = state();
    if (nowState == IOState::EndOfFile) {
        isReadCompleted_ = true;
    } else {
        isReadCompleted_ = false;
    }
    
    if (task_->callBack) {
        task_->callBack(this, std::move(data), nowState);
    }
}

void Reader::updateReadRange(Range range) noexcept {
    if (worker_.isExit()) {
        LogE("Reader is exit.");
        return;
    }
    if (!range.isValid()) {
        return;
    }
    task_->range = range;
    seekPos_ = range.pos;
    worker_.start();
}

int64_t Reader::tell() noexcept {
    int64_t pos = 0;
    file_.withReadLock([&](auto& file){
        if (file) {
            pos = static_cast<int64_t>(file->tell());
        }
    });
    return pos;
}

uint64_t Reader::size() noexcept {
    uint64_t size = 0;
    file_.withReadLock([&](auto& file){
        if (file) {
            size = file->fileSize();
        }
    });
    return size;
}

}//end namespace slark

