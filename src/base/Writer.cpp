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
    file_.withWriteLock([&](auto& file){
        file = std::make_unique<FileUtil::WriteFile>(std::string(path), isAppend);
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

void Writer::release() noexcept {
    isStop_ = true;
    worker_.start();
}

IOState Writer::state() noexcept {
    if (!isOpen_ || isStop_) {
        return IOState::Closed;
    }
    IOState state = IOState::Normal;
    file_.withReadLock([&](auto& file){
        if (!file) {
            state = IOState::Closed;
        } else if(file->isFailed()) {
            state = IOState::Error;
        }
    });
    return state;
}

std::string_view Writer::path() noexcept {
    std::string_view path;
    file_.withReadLock([&](auto& file){
        if (file) {
            path = file->path();
        }
    });
    return path;
}

void Writer::process() noexcept {
    using namespace std::chrono_literals;
    std::vector<DataPtr> dataList;
    dataList_.withLock([&](auto& vec){
        dataList.swap(vec);
    });
    if (dataList.empty()) {
        if (!isOpen_ || isStop_) {
            file_.withWriteLock([](auto& file){
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
        } else {
            constexpr auto kMaxIdleTime = 3000ms;
            if ((Time::nowTimeStamp() - idleTime_).toMilliSeconds() > kMaxIdleTime) {
                worker_.pause();
            }
        }
        return;
    }
    
    bool isSuccess = true;
    file_.withReadLock([&](auto& file){
        for (auto& data : dataList) {
            if (file->isFailed()){
                isSuccess = false;
                break;
            }
            file->write(*data);
        }
    });
    if (!isSuccess) {
        LogP("error, {} writer state is error.", worker_.getName());
    }
    idleTime_ = Time::nowTimeStamp();
}

uint32_t Writer::getHashId() noexcept {
    return worker_.getHashId();
}

uint32_t Writer::writeCount() noexcept {
    uint32_t count = 0;
    file_.withReadLock([&](auto& file){
        count = file->writeCount();
    });
    return count;
}

uint64_t Writer::writeSize() noexcept {
    uint64_t size = 0;
    file_.withReadLock([&](auto& file){
        size = file->writeSize();
    });
    return size;
}

bool Writer::write(DataPtr data) noexcept {
    if (!isOpen_ || isStop_) {
        LogE("unable to write data, file has been closed.");
        return false;
    }
    dataList_.withLock([&](auto& vec){
        vec.emplace_back(std::move(data));
    });
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

