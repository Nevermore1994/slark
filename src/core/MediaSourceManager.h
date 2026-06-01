//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#pragma once

#include "Error.h"
#include "IStream.h"
#include "NonCopyable.h"
#include "StreamFactory.hpp"

#include <functional>
#include <memory>
#include <mutex>

namespace slark {

struct DataPacket;

using StreamDataCallback = std::function<void(DataPacket)>;
using StreamErrorCallback = std::function<void(bool /*isNetwork*/)>;
using StreamCompleteCallback = std::function<void()>;

class MediaSourceManager : public NonCopyMovable {
public:
    MediaSourceManager() = default;
    ~MediaSourceManager() = default;

    bool open(const std::string& uri,
              StreamDataCallback onData,
              StreamErrorCallback onError,
              StreamCompleteCallback onComplete) noexcept;

    void start() noexcept;
    void pause() noexcept;
    void close() noexcept;

    [[nodiscard]] StreamResult<void> seek(uint64_t position) noexcept;
    [[nodiscard]] StreamResult<void> seekByTime(double timeSec) noexcept;
    void updateRange(Range range) noexcept;

    [[nodiscard]] const StreamInfo& streamInfo() const noexcept {
        std::lock_guard<std::mutex> lock(mutex_);
        if (stream_) {
            return stream_->info();
        }
        return streamInfo_;
    }

    [[nodiscard]] StreamType streamType() const noexcept {
        return streamInfo().type;
    }

    [[nodiscard]] bool isNetwork() const noexcept {
        auto t = streamInfo().type;
        return t == StreamType::Http || t == StreamType::HLS;
    }

    [[nodiscard]] bool isLive() const noexcept {
        return streamInfo().isLive;
    }

    [[nodiscard]] bool isValid() const noexcept {
        return stream_ != nullptr;
    }

private:
    mutable std::mutex mutex_;
    std::shared_ptr<IStream> stream_;
    StreamInfo streamInfo_;
};

} // namespace slark
