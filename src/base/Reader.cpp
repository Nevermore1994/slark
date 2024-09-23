//
// Created by Nevermore on 2024/3/19.
// slark ReadHandler
// Copyright (c) 2024 Nevermore All rights reserved.
//
#include "Reader.hpp"
#include "Log.hpp"
#include "FileUtil.h"
#include "Util.hpp"

namespace slark {

static const std::string kReaderPrefixName = "Reader_";

Reader::Reader()
    : worker_(Util::genRandomName(kReaderPrefixName), &Reader::process, this) {
    
}

bool Reader::open(std::string_view path, ReaderSetting&& setting) {
    bool isSuccess = false;
    file_.withWriteLock([&](auto& file){
        file = std::make_unique<FileUtil::ReadFile>(std::string(path));
        setting_ = std::move(setting);
        isSuccess = file->open();
    });
    worker_.setInterval(setting.timeInterval);
    return isSuccess;
}

Reader::~Reader() = default;

IOState Reader::state() noexcept {
    IOState state = IOState::Normal;
    file_.withReadLock([&](auto& file){
        if (!file || worker_.isExit()) {
            state = IOState::Closed;
        } else if (!worker_.isRunning()) {
            state = IOState::Pause;
        } else if (file->isFailed()) {
            state = IOState::Error;
        } else if(file->readOver()) {
            state = IOState::EndOfFile;
        }
    });
    return state;
}

void Reader::start() noexcept {
    worker_.start();
}

void Reader::resume() noexcept {
    worker_.resume();
}

void Reader::pause() noexcept {
    worker_.pause();
}

void Reader::close() noexcept {
    worker_.pause();
    file_.withWriteLock([](auto& file){
        if (file) {
            file->close();
            LogI("{} readr closed", file->path());
        }
        file.reset();
    });
}

void Reader::stop() noexcept {
    close();
    worker_.stop();
}

std::string_view Reader::path() noexcept {
    std::string_view path;
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
    seekPos_ = static_cast<int64_t>(pos);
}

void Reader::process() {
    auto nowState = state();
    if (nowState == IOState::Error) {
        worker_.pause();
        LogE("Reader is in error state.");
        return;
    } else if (nowState == IOState::EndOfFile) {
        worker_.pause();
        LogI("read data completed.");
        return;
    }
    Data data(setting_.readBlockSize);
    int64_t offset = 0;
    file_.withReadLock([&](auto& file){
        if (!file) {
            return;
        }
        if (seekPos_.has_value()) {
            file->seek(seekPos_.value());
            seekPos_.reset();
        }

        offset = file->tell();
        file->read(data);
    });

    if (setting_.callBack) {
        setting_.callBack(data.detachData(), offset, state());
    }
}

int64_t Reader::tell() noexcept {
    int64_t pos = 0;
    file_.withReadLock([&](auto& file){
        if (file) {
            pos = file->tell();
        }
    });
    return pos;
}

}//end namespace slark

