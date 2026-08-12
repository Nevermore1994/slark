//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#include "MediaSourceManager.h"

#include "Data.h"
#include "HLSStream.h"
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

    // Network streams need the owner thread injected at construction; local file streams do not
    if (detectedType == StreamType::Http || detectedType == StreamType::HLS) {
        LogA(ownerThread_ != nullptr,
             "setOwnerThread must be called before open for network streams");
    }
    LogI("[MediaSourceManager::open] uri={} detectedType={}", uri, static_cast<int>(detectedType));

    auto result = StreamFactory::createStream(uri, ownerThread_);
    if (!result.has_value()) {
        LogE("MediaSourceManager createStream failed: {}", result.error().message);
        return false;
    }
    stream_ = std::move(result.value());

    LogI("[MediaSourceManager::open] stream created: type={}",
         static_cast<int>(stream_->info().type));

    OpenParams params;
    params.uri = uri;

    // Share the callback so the EOS packet follows data packets on the same channel.
    auto dataCallback = std::make_shared<StreamDataCallback>(std::move(onData));

    params.onData = [dataCallback](DataChunk chunk) {
        if (!dataCallback || !*dataCallback || chunk.empty()) {
            return;
        }
        DataPacket packet;
        packet.data = std::make_unique<Data>(chunk.data);
        packet.offset = static_cast<int64_t>(chunk.offset);
        packet.segmentIndex = chunk.segmentIndex;
        packet.tag = std::to_string(chunk.segmentIndex);
        packet.isLastChunk = chunk.isLastChunk;
        (*dataCallback)(std::move(packet));
    };

    params.onError = [onError = std::move(onError)](Error err, int32_t originErrorCode) {
        if (onError) {
            onError(err, originErrorCode);
        }
    };

    params.onComplete = [dataCallback, onComplete = std::move(onComplete)]() mutable {
        if (dataCallback && *dataCallback) {
            DataPacket packet;
            packet.isEndOfStream = true;
            (*dataCallback)(std::move(packet));
        }
        if (onComplete) {
            onComplete();
        }
    };

    auto openResult = stream_->open(params);
    if (!openResult.has_value()) {
        LogE("MediaSourceManager stream open failed.");
        stream_.reset();
        return false;
    }
    streamInfo_ = openResult.value();
    streamInfo_.uri = uri;
    LogI("[MediaSourceManager::open] stream open ok: totalSize={} isSeekable={}",
         streamInfo_.totalSize, streamInfo_.isSeekable);
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

void MediaSourceManager::setProductionGate(std::function<bool()> gate) noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    if (auto* hls = dynamic_cast<HLSStream*>(stream_.get())) {
        hls->setProductionGate(std::move(gate));
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

}  // namespace slark
