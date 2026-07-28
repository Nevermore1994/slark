//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#pragma once

#include <expected>
#include <functional>
#include <memory>
#include <optional>
#include <string>

#include "Data.h"
#include "DataView.hpp"
#include "Error.h"
#include "Range.h"

namespace slark {

enum class StreamType : uint8_t {
    File,
    Http,
    HLS,
};

enum class StreamState : uint8_t {
    Idle,
    Opening,
    Ready,
    Reading,
    Paused,
    Completed,
    Error,
};

template <typename T>
using StreamResult = std::expected<T, Error>;

struct DataChunk {
    DataView data;
    DataRefPtr owner;
    uint64_t offset = 0;
    uint32_t segmentIndex = 0;
    bool isLastChunk = false;

    [[nodiscard]] bool empty() const noexcept {
        return data.empty();
    }

    [[nodiscard]] size_t size() const noexcept {
        return data.size();
    }

    [[nodiscard]] bool valid() const noexcept {
        return owner != nullptr;
    }
};

struct StreamInfo {
    StreamType type = StreamType::File;
    std::string uri;
    uint64_t totalSize = 0;
    double duration = 0.0;
    bool isSeekable = true;
    bool isLive = false;
    std::string contentType;
};

using DataCallback = std::function<void(DataChunk)>;

using MetadataCallback = std::function<void(std::shared_ptr<void>)>;

using ErrorCallback = std::function<void(Error /*err*/, int32_t /*originErrorCode*/)>;

using CompletionCallback = std::function<void()>;

struct OpenParams {
    std::string uri;
    Range range;
    uint32_t readChunkSize = 64 * 1024;

    DataCallback onData;
    MetadataCallback onMetadata;
    ErrorCallback onError;
    CompletionCallback onComplete;
};

class IStream {
 public:
    virtual ~IStream() = default;

    virtual auto open(const OpenParams& params) -> StreamResult<StreamInfo> = 0;
    virtual void close() noexcept = 0;
    virtual void start() noexcept = 0;
    virtual void pause() noexcept = 0;
    virtual auto seek(uint64_t position) -> StreamResult<void> = 0;

    /// 基于时间的 seek，默认返回 InvalidOperation；HLS 实现此接口
    virtual auto seekByTime(double /*timeSec*/) -> StreamResult<void> {
        return std::unexpected(Error::InvalidOperation);
    }

    virtual void updateRange(Range range) noexcept = 0;

    [[nodiscard]] virtual StreamState state() const noexcept = 0;
    [[nodiscard]] virtual const StreamInfo& info() const noexcept = 0;
    [[nodiscard]] virtual uint64_t position() const noexcept = 0;
    [[nodiscard]] virtual bool isRunning() const noexcept = 0;
    [[nodiscard]] virtual bool isCompleted() const noexcept = 0;
};

using StreamPtr = std::unique_ptr<IStream>;

}  // namespace slark
