//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#include "MediaSourceManager.h"

#include "Data.h"
#include "Log.hpp"

namespace slark {

bool MediaSourceManager::open(const std::string& uri,
                              StreamDataCallback onData,
                              StreamErrorCallback onError,
                              StreamCompleteCallback onComplete) noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    if (uri.empty()) {
        LogE("MediaSourceManager open failed: uri is empty.");
        return false;
    }

    auto detectedType = StreamFactory::detectStreamType(uri);
    LogI("[MediaSourceManager::open] uri={} detectedType={}", uri, static_cast<int>(detectedType));

    auto result = StreamFactory::createStream(uri);
    if (!result.has_value()) {
        LogE("MediaSourceManager createStream failed: {}", result.error().message);
        return false;
    }
    stream_ = std::move(result.value());

    LogI("[MediaSourceManager::open] stream created: type={}", static_cast<int>(stream_->info().type));

    OpenParams params;
    params.uri = uri;

    params.onData = [onData = std::move(onData)](DataChunk chunk) {
        if (!onData || chunk.empty()) {
            return;
        }
        DataPacket packet;
        packet.data = std::make_unique<Data>(chunk.data);
        packet.offset = static_cast<int64_t>(chunk.offset);
        onData(std::move(packet));
    };

    params.onError = [onError = std::move(onError)](Error err, int32_t originErrorCode) {
        if (onError) {
            onError(err, originErrorCode);
        }
    };

    params.onComplete = std::move(onComplete);

    auto openResult = stream_->open(params);
    if (!openResult.has_value()) {
        LogE("MediaSourceManager stream open failed.");
        stream_.reset();
        return false;
    }
    streamInfo_ = openResult.value();
    streamInfo_.uri = uri;
    LogI("[MediaSourceManager::open] stream open ok: totalSize={} isSeekable={}",
         streamInfo_.totalSize,
         streamInfo_.isSeekable);
    return true;
}

void MediaSourceManager::start() noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    if (stream_) {
        LogI("[MediaSourceManager::start]");
        stream_->start();
    }
}

void MediaSourceManager::pause() noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    if (stream_) {
        stream_->pause();
    }
}

void MediaSourceManager::close() noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    if (stream_) {
        LogI("[MediaSourceManager::close]");
        stream_->close();
        stream_.reset();
    }
    streamInfo_ = {};
}

StreamResult<void> MediaSourceManager::seek(uint64_t position) noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!stream_) {
        return std::unexpected(Error::StreamNotReady);
    }
    return stream_->seek(position);
}

StreamResult<void> MediaSourceManager::seekByTime(double timeSec) noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!stream_) {
        return std::unexpected(Error::StreamNotReady);
    }
    return stream_->seekByTime(timeSec);
}

void MediaSourceManager::updateRange(Range range) noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    if (stream_) {
        stream_->updateRange(range);
    }
}

} // namespace slark
