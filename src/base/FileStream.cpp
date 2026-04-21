//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#include "FileStream.h"

namespace slark {

FileStream::FileStream() = default;

FileStream::~FileStream() {
    close();
}

auto FileStream::open(const OpenParams& params) -> StreamResult<StreamInfo> {
    params_ = params;
    readRange_ = params.range;

    file_ = std::make_unique<File::ReadFile>(params.uri);
    auto result = file_->open();
    if (!result) {
        return std::unexpected(Error::FileNotFound);
    }

    fileSize_ = file_->fileSize();

    info_.type = StreamType::File;
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
        thread_->request_stop();
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

    const bool kWasPaused = (state_ == StreamState::Paused);
    shouldStop_ = false;
    shouldPause_ = false;
    isRunning_ = true;
    state_ = StreamState::Reading;

    if (kWasPaused && thread_ && thread_->joinable()) {
        // Paused 状态：旧线程仍在 cv_.wait，直接唤醒，不新建线程
        notifyThread();
        return;
    }

    thread_ = std::make_unique<std::jthread>([this] {
        readLoop();
    });
}

void FileStream::pause() noexcept {
    if (state_ != StreamState::Reading) {
        return;
    }

    shouldPause_ = true;
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

    // seek 投递到 readLoop 线程执行，避免与 readLoop 中的 file_->readInto 竞争
    {
        std::lock_guard lock(mutex_);
        pendingSeekPos_ = position;
        pendingSeekFlag_ = true;
    }
    notifyThread();
    return {};
}

void FileStream::updateRange(Range range) noexcept {
    readRange_ = range;
}

void FileStream::readLoop() noexcept {
    while (!shouldStop_) {
        {
            std::unique_lock lock(mutex_);
            cv_.wait(lock, [this] {
                return !shouldPause_ || shouldStop_ || pendingSeekFlag_;
            });
        }

        if (shouldStop_) {
            break;
        }

        // 处理 seek 请求
        if (pendingSeekFlag_) {
            uint64_t seekPos = 0;
            {
                std::lock_guard lock(mutex_);
                seekPos = pendingSeekPos_;
                pendingSeekFlag_ = false;
            }
            if (file_) {
                auto result = file_->seek(static_cast<int64_t>(seekPos));
                if (result) {
                    position_ = seekPos;
                }
            }
            continue;
        }

        auto buffer = std::make_shared<Data>(params_.readChunkSize);
        if (!buffer || buffer->capacity() == 0) {
            if (params_.onError) {
                params_.onError(Error::OutOfMemory);
            }
            break;
        }

        auto readResult = file_->readInto(*buffer, params_.readChunkSize);
        if (!readResult || *readResult == 0) {
            isCompleted_ = true;
            state_ = StreamState::Completed;
            isRunning_ = false;

            if (params_.onComplete) {
                params_.onComplete();
            }
            break;
        }

        buffer->setSize(*readResult);

        if (readRange_.isValid() && readRange_.size.has_value()) {
            auto endPos = readRange_.start() + static_cast<uint64_t>(readRange_.size.value());
            if (position_ + *readResult >= endPos) {
                auto remaining = endPos - position_;
                buffer->setSize(remaining);

                if (params_.onData) {
                    params_.onData(DataChunk{
                        .data = DataView(buffer->view()),
                        .owner = std::move(buffer),
                        .offset = position_,
                        .segmentIndex = 0,
                        .isLastChunk = true,
                    });
                }

                isCompleted_ = true;
                state_ = StreamState::Completed;
                isRunning_ = false;

                if (params_.onComplete) {
                    params_.onComplete();
                }
                break;
            }
        }

        if (params_.onData) {
            params_.onData(DataChunk{
                .data = DataView(buffer->view()),
                .owner = std::move(buffer),
                .offset = position_,
                .segmentIndex = 0,
                .isLastChunk = false,
            });
        }

        position_ += *readResult;
    }
}

void FileStream::notifyThread() noexcept {
    cv_.notify_all();
}

} // namespace slark
