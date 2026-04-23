//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#pragma once

#include "Data.h"
#include "DataView.hpp"
#include "StringUtil.h"
#include "Type.h"

#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>

namespace slark::http {

class HttpResponseParser {
public:
    enum class State : uint8_t {
        StatusLine,
        Headers,
        BodyFixed,
        BodyChunked,
        ChunkSize,
        ChunkData,
        ChunkTrailer,
        Completed,
        Error,
    };

    struct Callbacks {
        std::function<void(HttpStatusCode, const std::string&)> onStatusLine;
        std::function<void(std::unordered_map<std::string, std::string>&&)> onHeaders;
        std::function<void(const Byte*, size_t)> onBodyData;
        std::function<void()> onCompleted;
        std::function<void(ResultCode)> onError;
    };

    explicit HttpResponseParser(Callbacks callbacks) : callbacks_(std::move(callbacks)) {}

    // 设置请求方法，用于判断 HEAD 请求的响应无 body
    void setRequestMethod(HttpMethodType method) noexcept {
        requestMethod_ = method;
    }

    // 设置性能统计对象
    void setPerf(RequestPerf* perf) noexcept {
        perf_ = perf;
    }

    // 喂入数据，驱动状态机
    void feed(const Byte* data, size_t size);

    // EOF 到达时的终结处理
    // 对于无 Content-Length 且非 chunked 的响应，EOF 标识消息结束
    void onEof() {
        if (state_ == State::Completed || state_ == State::Error) {
            return;
        }

        // 仅对未知长度的 body 以 EOF 作为结束标志
        if (state_ == State::BodyFixed && contentLength_ < 0) {
            complete();
        } else if (state_ == State::StatusLine || state_ == State::Headers) {
            // 头部未完整接收即断开
            fail(ResultCode::Disconnected);
        }
        // 其他状态（如 chunked 中途断开）视为错误
        else if (state_ != State::Completed) {
            fail(ResultCode::Disconnected);
        }
    }

    [[nodiscard]] State state() const noexcept {
        return state_;
    }

    [[nodiscard]] bool isCompleted() const noexcept {
        return state_ == State::Completed;
    }

    [[nodiscard]] bool hasError() const noexcept {
        return state_ == State::Error;
    }

    [[nodiscard]] bool shouldCloseConnection() const noexcept {
        return shouldClose_;
    }

    [[nodiscard]] bool isBodyUnknownLength() const noexcept {
        return state_ == State::BodyFixed && contentLength_ < 0;
    }

    [[nodiscard]] const std::unordered_map<std::string, std::string>& headers() const noexcept {
        return headers_;
    }

    void reset() noexcept;

private:
    void processStatusLine();
    void processHeaders();
    void processBodyFixed();
    void processChunkSize();
    void processChunkData();
    void processChunkTrailer();

    std::string findHeader(std::string_view key) const;

    // 统一失败出口，避免重复的状态设置和回调调用
    void fail(ResultCode code) noexcept {
        state_ = State::Error;
        if (callbacks_.onError) {
            callbacks_.onError(code);
        }
    }

    // 统一完成出口
    void complete() noexcept {
        state_ = State::Completed;
        if (callbacks_.onCompleted) {
            callbacks_.onCompleted();
        }
    }

    // RFC 7230：判断响应是否应该跳过 body
    [[nodiscard]] bool shouldSkipBody() const noexcept {
        // 1xx 信息性响应无 body
        auto code = static_cast<uint16_t>(statusCode_);
        if (code >= 100 && code < 200) {
            return true;
        }
        // 204 No Content 和 304 Not Modified 无 body
        if (statusCode_ == HttpStatusCode::NoContent ||
            statusCode_ == HttpStatusCode::NotModified) {
            return true;
        }
        // HEAD 请求的响应无 body
        if (requestMethod_ == HttpMethodType::Head) {
            return true;
        }
        return false;
    }

    static constexpr std::string_view kCRLF = "\r\n";
    static constexpr std::string_view kHeaderEnd = "\r\n\r\n";
    static constexpr size_t kCompactionThreshold = 8192; // 碎片化阈值
    static constexpr size_t kMaxTrailerSize = 16384;     // trailer 最大累计长度

    State state_ = State::StatusLine;
    Callbacks callbacks_;
    std::string buffer_;
    size_t readPos_ = 0; // 逻辑消费位置，避免频繁 erase
    std::unordered_map<std::string, std::string> headers_;
    HttpStatusCode statusCode_ = HttpStatusCode::Unknown;
    bool shouldClose_ = false;
    HttpMethodType requestMethod_ = HttpMethodType::Unknown;

    // body 相关
    int64_t contentLength_ = -1;
    int64_t bodyReceived_ = 0;
    int64_t currentChunkSize_ = 0;
    int64_t chunkBytesRead_ = 0;
    size_t trailerBytesRead_ = 0;

    // 性能统计
    RequestPerf* perf_ = nullptr;

private:
    // 获取当前可读数据
    [[nodiscard]] std::string_view readableData() const noexcept {
        return std::string_view(buffer_).substr(readPos_);
    }

    // 消费指定长度的数据
    void consume(size_t size) noexcept {
        readPos_ += size;
        // 当碎片化超过阈值时执行 compaction
        if (readPos_ > kCompactionThreshold) {
            compact();
        }
    }

    // 压缩缓冲区
    void compact() noexcept {
        if (readPos_ > 0) {
            buffer_.erase(0, readPos_);
            readPos_ = 0;
        }
    }

    // 获取可读数据大小
    [[nodiscard]] size_t readableSize() const noexcept {
        return buffer_.size() - readPos_;
    }
};

} // namespace slark::http
