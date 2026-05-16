//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#include "Writer.h"

#include "Log.hpp"
#include "Util.h"

namespace slark {

static const std::string kWriterPrefixName = "Writer_";

Writer::Writer() : worker_(Thread::create(Util::genRandomName(kWriterPrefixName))) {
    using namespace std::chrono_literals;
    worker_->postRepeatTask(0ms, [this]() {
        process();
    });
}

Writer::~Writer() {
    dispose();
}

void Writer::closeFile() noexcept {
    file_.withLock([](auto& file) {
        if (file) {
            file->close();
            file.reset();
        }
    });
}

bool Writer::open(std::string_view path, bool isAppend) noexcept {
    bool isSuccess = false;
    file_.withLock([&](auto& file) {
        file = std::make_unique<File::WriteFile>(std::string(path), isAppend);
        auto result = file->open();
        if (!result) {
            LogE("open file failed. {}", path);
        }
        isSuccess = result.has_value();
    });
    if (isSuccess) {
        isOpen_.store(true, std::memory_order_release);
        worker_->start();
    }
    return isSuccess;
}

void Writer::close() noexcept {
    isOpen_.store(false, std::memory_order_release);
    worker_->start();
}

void Writer::dispose() noexcept {
    isStop_.store(true, std::memory_order_release);
    worker_->stop();
}

IOState Writer::state() noexcept {
    if (!isOpen_.load(std::memory_order_acquire) || isStop_.load(std::memory_order_acquire)) {
        return IOState::Closed;
    }
    return file_.withLock([](auto& file) -> IOState {
        if (!file) {
            return IOState::Closed;
        }
        return file->isOpen() ? IOState::Normal : IOState::Error;
    });
}

std::string Writer::path() noexcept {
    return file_.withLock([](auto& file) -> std::string {
        return file ? file->path().string() : std::string{};
    });
}

bool Writer::checkState() noexcept {
    if (!isOpen_.load(std::memory_order_acquire) || isStop_.load(std::memory_order_acquire)) {
        closeFile();
        if (!isOpen_.load(std::memory_order_acquire)) {
            worker_->pause();
            LogI("{} closed", worker_->getName());
        } else {
            worker_->stop();
            LogI("{} stopped", worker_->getName());
        }
        return false;
    }
    if (dataQueue_.empty()) {
        worker_->pause();
        return false;
    }
    return true;
}

void Writer::process() noexcept {
    static constexpr uint32_t kMaxWriteCount = 1024;
    if (!checkState()) {
        return;
    }
    file_.withLock([&](auto& file) {
        if (!file || !file->isOpen()) {
            return;
        }
        for (uint32_t i = 0; i < kMaxWriteCount; ++i) {
            DataPtr data;
            if (!dataQueue_.tryPop(data)) {
                break;
            }
            file->write(*data);
        }
    });
}

uint32_t Writer::getHashId() noexcept {
    return worker_->getHashId();
}

uint32_t Writer::writeCount() noexcept {
    return file_.withLock([](auto& file) -> uint32_t {
        return file ? file->writeCount() : 0;
    });
}

uint64_t Writer::writeSize() noexcept {
    return file_.withLock([](auto& file) -> uint64_t {
        return file ? file->writeSize() : 0;
    });
}

bool Writer::write(DataPtr data) noexcept {
    if (!isOpen_.load(std::memory_order_acquire) || isStop_.load(std::memory_order_acquire)) {
        LogE("unable to write data, file has been closed.");
        return false;
    }
    dataQueue_.push(std::move(data));
    worker_->start();
    return true;
}

bool Writer::write(const Data& data) noexcept {
    return write(data.copy());
}

bool Writer::write(std::string_view str) noexcept {
    return write(std::make_unique<Data>(str));
}

} // namespace slark
