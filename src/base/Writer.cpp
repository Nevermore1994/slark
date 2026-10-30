//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#include "Writer.h"

#include <algorithm>
#include <utility>
#include <vector>

#include "Log.hpp"
#include "Util.h"

namespace slark {
namespace {
constexpr size_t kMaxBatchSize = 1024;
const std::string kWriterPrefixName = "Writer_";
}  // namespace

Writer::Writer() : worker_(TaskThread::create(Util::genRandomName(kWriterPrefixName))) {}

Writer::~Writer() {
    dispose();
}

bool Writer::Completion::wait() noexcept {
    std::unique_lock lock(mutex);
    cond.wait(lock, [this] {
        return done;
    });
    return result;
}

void Writer::Completion::complete(bool value) noexcept {
    {
        std::lock_guard lock(mutex);
        if (done) {
            return;
        }
        result = value;
        done = true;
    }
    cond.notify_all();
}

bool Writer::open(std::string_view path, bool isAppend) noexcept {
    auto completion = std::make_shared<Completion>();
    if (!enqueue(OpenItem{std::string(path), isAppend, completion})) {
        return false;
    }
    return completion->wait();
}

void Writer::close() noexcept {
    auto completion = std::make_shared<Completion>();
    if (enqueue(CloseItem{completion})) {
        (void)completion->wait();
    }
}

void Writer::dispose() noexcept {
    if (!worker_) {
        std::lock_guard lock(mutex_);
        disposed_ = true;
        return;
    }

    auto completion = std::make_shared<Completion>();
    if (enqueue(StopItem{completion}, true)) {
        (void)completion->wait();
        worker_->stop();
    }
    worker_->join();
}

std::string Writer::path() noexcept {
    return file_.withLock([](auto& file) -> std::string {
        return file ? file->path().string() : std::string{};
    });
}

IOState Writer::state() noexcept {
    {
        std::lock_guard lock(mutex_);
        if (disposed_) {
            return IOState::Closed;
        }
    }
    if (!isOpen_.load(std::memory_order_acquire)) {
        return IOState::Closed;
    }
    return file_.withLock([](auto& file) -> IOState {
        if (!file) {
            return IOState::Closed;
        }
        return file->isOpen() ? IOState::Normal : IOState::Error;
    });
}

bool Writer::write(DataPtr data) noexcept {
    if (!data) {
        return false;
    }
    return enqueue(DataItem{std::move(data)});
}

bool Writer::write(const Data& data) noexcept {
    return write(data.copy());
}

bool Writer::write(std::string_view str) noexcept {
    return write(std::make_unique<Data>(str));
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

uint32_t Writer::getHashId() noexcept {
    return worker_ ? worker_->getHashId() : 0;
}

bool Writer::enqueue(Item item, bool stopping) noexcept {
    if (!worker_) {
        return false;
    }
    std::lock_guard lock(mutex_);
    if (disposed_) {
        return false;
    }
    if (stopping) {
        disposed_ = true;
    }
    inbox_.push_back(std::move(item));
    if (!scheduled_) {
        scheduled_ = true;
        auto result = worker_->postTask([this] {
            drain();
        });
        if (!result) {
            scheduled_ = false;
            inbox_.pop_back();
            if (stopping) {
                disposed_ = false;
            }
            return false;
        }
    }
    return true;
}

void Writer::drain() noexcept {
    std::vector<Item> batch;
    batch.reserve(kMaxBatchSize);
    {
        std::lock_guard lock(mutex_);
        if (inbox_.empty()) {
            scheduled_ = false;
            return;
        }
        const auto count = std::min(kMaxBatchSize, inbox_.size());
        for (size_t i = 0; i < count; ++i) {
            batch.push_back(std::move(inbox_.front()));
            inbox_.pop_front();
        }
    }

    for (auto& item : batch) {
        process(std::move(item));
    }

    std::lock_guard lock(mutex_);
    if (inbox_.empty()) {
        scheduled_ = false;
        return;
    }
    auto result = worker_->postTask([this] {
        drain();
    });
    if (!result) {
        scheduled_ = false;
    }
}

void Writer::process(Item item) noexcept {
    std::visit(
        [this](auto&& value) {
            using T = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<T, DataItem>) {
                writeData(std::move(value.data));
            } else if constexpr (std::is_same_v<T, OpenItem>) {
                if (fileOpen_) {
                    closeFile();
                }
                fileOpen_ = openFile(value.path, value.append);
                isOpen_.store(fileOpen_, std::memory_order_release);
                if (fileOpen_) {
                    flushParked();
                }
                value.completion->complete(fileOpen_);
            } else if constexpr (std::is_same_v<T, CloseItem>) {
                closeFile();
                fileOpen_ = false;
                isOpen_.store(false, std::memory_order_release);
                value.completion->complete(true);
            } else {
                closeFile();
                fileOpen_ = false;
                isOpen_.store(false, std::memory_order_release);
                const auto dropped = parked_.size();
                parked_.clear();
                parkedBytes_ = 0;
                value.completion->complete(true);
                if (dropped > 0) {
                    LogP("writer dropped {} items", dropped);
                }
            }
        },
        std::move(item));
}

bool Writer::openFile(const std::string& path, bool append) noexcept {
    bool opened = false;
    file_.withLock([&](auto& file) {
        file = std::make_unique<File::WriteFile>(path, append, false);
        const auto result = file->open();
        opened = result.has_value();
        if (!opened) {
            LogP("open file failed. {}", path);
        }
    });
    return opened;
}

void Writer::closeFile() noexcept {
    file_.withLock([](auto& file) {
        if (file) {
            file->close();
            file.reset();
        }
    });
}

void Writer::writeData(DataPtr data) noexcept {
    if (!data) {
        return;
    }
    if (!fileOpen_) {
        park(std::move(data));
        return;
    }
    file_.withLock([&](auto& file) {
        if (file && file->isOpen() && !file->write(*data)) {
            LogP("writer failed to write data to {}", file->path().string());
        }
    });
}

void Writer::park(DataPtr data) noexcept {
    parkedBytes_ += data->length();
    parked_.push_back(std::move(data));
    while (parkedBytes_ > kMaxParkedBytes && parked_.size() > 1) {
        parkedBytes_ -= parked_.front()->length();
        parked_.pop_front();
    }
}

void Writer::flushParked() noexcept {
    parkedBytes_ = 0;
    while (!parked_.empty()) {
        auto data = std::move(parked_.front());
        parked_.pop_front();
        writeData(std::move(data));
    }
}

}  // namespace slark
