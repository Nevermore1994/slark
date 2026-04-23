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

    auto result = StreamFactory::createStream(uri);
    if (!result.has_value()) {
        LogE("MediaSourceManager createStream failed: {}", result.error().message);
        return false;
    }
    stream_ = std::move(result.value());

    // 提前判断是否网络流，open 后 streamInfo_ 才填充
    const bool kIsNetworkStream = StreamFactory::isHttp(uri) || StreamFactory::isHLS(uri);

    OpenParams params;
    params.uri = uri;

    // DataChunk → DataPacket 转换，零拷贝：直接从 DataChunk.data 构造 Data
    params.onData = [onData = std::move(onData)](DataChunk chunk) {
        if (!onData || chunk.empty()) {
            return;
        }
        DataPacket packet;
        packet.data = std::make_unique<Data>(chunk.data);
        packet.offset = static_cast<int64_t>(chunk.offset);
        onData(std::move(packet));
    };

    params.onError = [onError = std::move(onError), kIsNetworkStream](const Error&) {
        if (onError) {
            onError(kIsNetworkStream);
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
    return true;
}

void MediaSourceManager::start() noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    if (stream_) {
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
        stream_->close();
        stream_.reset();
    }
    streamInfo_ = {};
}

void MediaSourceManager::seek(uint64_t position) noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    if (stream_) {
        auto res = stream_->seek(position);
        if (!res.has_value()) {
            LogE("MediaSourceManager seek failed at position:{}", position);
        }
    }
}

void MediaSourceManager::updateRange(Range range) noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    if (stream_) {
        stream_->updateRange(range);
    }
}

} // namespace slark
