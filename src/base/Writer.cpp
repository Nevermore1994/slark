//
//  WriteHandler.cpp
//  slark
//
//  Copyright (c) 2023 Nevermore All rights reserved.
//

#include "Writer.hpp"
#include "Util.hpp"
#include "Log.hpp"

namespace slark {

static const std::string kWriterPrefixName = "Writer_";
Writer::Writer()
    : worker_(Util::genRandomName(kWriterPrefixName), &Writer::process, this)
{

}

Writer::~Writer() = default;

bool Writer::open(std::string_view path, bool isAppend) noexcept {
    bool isSuccess = false;
    file_.withLock([&](auto& file){
        file = std::make_unique<File::WriteFile>(std::string(path), isAppend);
        if (!file->open()) {
            LogE("open file failed. {}", path);
        }
        isSuccess = !file->isFailed();
    });
    if (isSuccess) {
        isOpen_ = true;
        worker_.start();
    }
    return isSuccess;
}

void Writer::close() noexcept {
    isOpen_ = false;
    worker_.start();
}

void Writer::dispose() noexcept {
    isStop_ = true;
    worker_.start();
}

IOState Writer::state() noexcept {
    if (!isOpen_.load() || isStop_.load()) {
        return IOState::Closed;
    }
    IOState state = IOState::Normal;
    file_.withLock([&](auto& file){
        if (!file) {
            state = IOState::Closed;
        } else if(file->isFailed()) {
            state = IOState::Error;
        }
    });
    return state;
}

std::string Writer::path() noexcept {
    std::string path;
    file_.withLock([&](auto& file){
        if (file) {
            path = file->path();
        }
    });
    return path;
}

bool Writer::checkState() noexcept {
    if (!isOpen_ || isStop_) {
        file_.withLock([](auto& file){
            if (file) {
                file->close();
            }
            file.reset();
        });
        if (!isOpen_) {
            worker_.pause();
            LogI("{} closed", worker_.getName());
        } else {
            worker_.stop();
            LogI("{} stoped", worker_.getName());
        }
        return false;
    }
    if (dataQueue_.empty()) {
        worker_.pause();
        return false;
    }
    return true;
}

void Writer::process() noexcept {
    constexpr static uint32_t kMaxWriteCount = 1024;
    if (!checkState()) {
        return;
    }
    file_.withLock([&](auto& file){
        if (file->isFailed()){
            return;
        }
        uint32_t writeCount = 0;
        while (!dataQueue_.empty() && writeCount < kMaxWriteCount) {
            if (file->isFailed()){
                break;
            }
            DataPtr data;
            if (dataQueue_.tryPop(data)) {
                file->write(*data);
                writeCount++;
            } else {
                break;
            }
        }
    });
}

uint32_t Writer::getHashId() noexcept {
    return worker_.getHashId();
}

uint32_t Writer::writeCount() noexcept {
    uint32_t count = 0;
    file_.withLock([&](auto& file){
        count = file->writeCount();
    });
    return count;
}

uint64_t Writer::writeSize() noexcept {
    uint64_t size = 0;
    file_.withLock([&](auto& file){
        size = file->writeSize();
    });
    return size;
}

bool Writer::write(DataPtr data) noexcept {
    if (!isOpen_ || isStop_) {
        LogE("unable to write data, file has been closed.");
        return false;
    }
    dataQueue_.push(std::move(data));
    worker_.start();
    return true;
}

bool Writer::write(Data&& data) noexcept {
    auto dataPtr = data.detachData();
    return write(std::move(dataPtr));
}

bool Writer::write(const Data& data) noexcept {
    auto dataPtr = data.copy();
    return write(std::move(dataPtr));
}

bool Writer::write(const std::string& str) noexcept {
    auto dataPtr = std::make_unique<Data>(str);
    return write(std::move(dataPtr));
}

bool Writer::write(std::string&& str) noexcept {
    auto dataPtr = std::make_unique<Data>(str);
    return write(std::move(dataPtr));
}

}

