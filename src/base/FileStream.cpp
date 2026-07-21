//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#include "FileStream.h"

#include "Log.hpp"

namespace slark {

FileStream::FileStream() = default;

FileStream::~FileStream() {
    close();
}

StreamResult<StreamInfo> FileStream::open(const OpenParams& params) {
    params_ = params;
    readRange_ = params.range;

    file_ = std::make_unique<File::ReadFile>(params.uri);
    auto result = file_->open();
    if (!result) {
        return std::unexpected(Error::FileNotFound);
    }

    fileSize_ = file_->fileSize();

    info_.type = StreamType::File;
    info_.uri = params.uri;
    info_.totalSize = fileSize_;
    info_.isSeekable = true;
    info_.isLive = false;

    if (readRange_.isValid()) {
        position_ = readRange_.start();
        auto seekResult = file_->seek(static_cast<int64_t>(position_));
        if (!seekResult) {
            return std::unexpected(Error::InvalidOperation);
        }
    }

    state_ = StreamState::Ready;
    return info_;
}

void FileStream::close() noexcept {
    shouldStop_ = true;
    notifyThread();

    if (thread_ && thread_->joinable()) {
        thread_->join();
    }

    if (file_) {
        file_->close();
        file_.reset();
    }

    state_ = StreamState::Idle;
    isRunning_ = false;
    isCompleted_ = false;
}

void FileStream::start() noexcept {
    if (state_ != StreamState::Ready && state_ != StreamState::Paused) {
        return;
    }

    if (!file_) {
        return;
    }

    const bool wasPaused = (state_ == StreamState::Paused);
    shouldStop_ = false;
    shouldPause_ = false;
    isRunning_ = true;
    state_ = StreamState::Reading;

    if (wasPaused && thread_ && thread_->joinable()) {
        notifyThread();
        return;
    }

    thread_ = std::make_unique<std::thread>([this] {
        readLoop();
    });
}

void FileStream::pause() noexcept {
    if (state_ != StreamState::Reading && state_ != StreamState::Completed) {
        return;
    }

    shouldPause_ = true;
    eofReached_.store(false, std::memory_order_release);
    notifyThread();
    state_ = StreamState::Paused;
    isRunning_ = false;
}

auto FileStream::seek(uint64_t position) -> StreamResult<void> {
    if (!file_) {
        return std::unexpected(Error::InvalidOperation);
    }

    if (position >= fileSize_) {
        return std::unexpected(Error::OutOfRange);
    }

    {
        std::lock_guard lock(mutex_);
        pendingSeekPos_ = position;
        seekCompleted_ = false;
    }
    pendingSeekFlag_.store(true, std::memory_order_release);
    notifyThread();

    {
        std::unique_lock lock(mutex_);
        if (!cv_.wait_for(lock, std::chrono::seconds(5), [this] { return seekCompleted_; })) {
            LogW("seek: wait for completion timeout, pos={}", pendingSeekPos_);
        }
    }
    return {};
}

void FileStream::updateRange(Range range) noexcept {
    readRange_ = range;
}

bool FileStream::deliverData(std::shared_ptr<Data> buffer, bool eof) noexcept {
    if (!buffer || buffer->empty()) {
        return false;
    }
    if (eof) {
        isCompleted_ = true;
        state_ = StreamState::Completed;
        isRunning_ = false;
        if (params_.onComplete) {
            params_.onComplete();
        }
        return false;
    }

    bool isLast = false;
    auto bufferSize = buffer->size();

    if (readRange_.isValid() && readRange_.size.has_value()) {
        auto endPos = readRange_.start() + static_cast<uint64_t>(readRange_.size.value());
        if (position_ + bufferSize >= endPos) {
            auto remaining = static_cast<size_t>(endPos - position_);
            buffer->setSize(remaining);
            bufferSize = remaining;
            isLast = true;
        }
    }

    if (params_.onData) {
        auto dataView = buffer->view();
        params_.onData(DataChunk{
            .data = DataView(dataView),
            .owner = std::move(buffer),
            .offset = position_,
            .segmentIndex = 0,
            .isLastChunk = isLast,
        });
    }

    if (isLast) {
        isCompleted_ = true;
        state_ = StreamState::Completed;
        isRunning_ = false;
        if (params_.onComplete) {
            params_.onComplete();
        }
        return false;
    }

    position_ += bufferSize;
    return true;
}

void FileStream::readLoop() noexcept {
    while (!shouldStop_) {
        if (pendingSeekFlag_.load(std::memory_order_acquire)) {
            eofReached_.store(false, std::memory_order_release);
            isCompleted_ = false;
            uint64_t seekPos = 0;
            {
                std::lock_guard lock(mutex_);
                seekPos = pendingSeekPos_;
                pendingSeekFlag_.store(false, std::memory_order_release);
            }
            if (file_) {
                auto result = file_->seek(static_cast<int64_t>(seekPos));
                if (result) {
                    position_ = seekPos;
                }
            }
            {
                std::lock_guard lock(mutex_);
                seekCompleted_ = true;
            }
            cv_.notify_all();
        }

        if (shouldPause_) {
            std::unique_lock lock(mutex_);
            cv_.wait(lock, [this] {
                return !shouldPause_ || shouldStop_ || pendingSeekFlag_.load(std::memory_order_acquire);
            });
            continue;
        }

        if (eofReached_.load(std::memory_order_acquire)) {
            std::unique_lock lock(mutex_);
            cv_.wait(lock, [this] {
                return !eofReached_.load(std::memory_order_acquire) || shouldStop_ ||
                       pendingSeekFlag_.load(std::memory_order_acquire);
            });
            continue;
        }

        if (shouldStop_) {
            break;
        }

        auto chunkSize = std::max<uint32_t>(params_.readChunkSize, 1);
        auto buffer = std::make_shared<Data>(chunkSize);
        if (!buffer || buffer->capacity() == 0) {
            LogE("readLoop: buffer alloc failed, chunkSize={}", chunkSize);
            if (params_.onError) {
                params_.onError(Error::OutOfMemory, 0);
            }
            break;
        }

        auto readResult = file_->readInto(*buffer, chunkSize);
        bool eof = (!readResult || *readResult == 0);
        if (!eof) {
            buffer->setSize(*readResult);
        }

        if (pendingSeekFlag_.load(std::memory_order_acquire)) {
            continue;
        }

        if (!deliverData(std::move(buffer), eof)) {
            LogI("readLoop: deliverData EOF at pos={}, park thread", position_.load());
            eofReached_.store(true, std::memory_order_release);
            continue;
        }
        LogI("readLoop: sent data at pos={}", position_.load());
    }
}

void FileStream::notifyThread() noexcept {
    cv_.notify_all();
}

} // namespace slark
