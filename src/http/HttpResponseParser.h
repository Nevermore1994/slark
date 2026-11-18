//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#pragma once

#include <chrono>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>

#include "Data.h"
#include "DataView.hpp"
#include "Log.hpp"
#include "StringUtil.h"
#include "Type.h"

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
        std::function<void(const std::unordered_map<std::string, std::string>&)> onHeaders;
        std::function<void(const std::unordered_map<std::string, std::string>&)> onTrailers;
        std::function<void(const Byte*, size_t)> onBodyData;
        std::function<void()> onCompleted;
        std::function<void(Error)> onError;
    };

    explicit HttpResponseParser(Callbacks callbacks) : callbacks_(std::move(callbacks)) {}

    void setRequestMethod(HttpMethodType method) noexcept {
        requestMethod_ = method;
    }

    void setMaxResponseBodySize(uint64_t size) noexcept {
        maxResponseBodySize_ = size;
    }

    void setPerf(RequestPerf* perf) noexcept {
        perf_ = perf;
    }

    void feed(const Byte* data, size_t size);

    void onEof() {
        if (state_ == State::Completed || state_ == State::Error) {
            return;
        }

        LogI("[HttpResponseParser::onEof] state={} bodyReceived={} contentLength={}",
             stateName(state_), bodyReceived_, contentLength_.value_or(kUnknownContentLength));

        if (state_ == State::BodyFixed && !contentLength_.has_value()) {
            endedByEof_ = true;
            complete();
        } else if (state_ == State::StatusLine || state_ == State::Headers) {
            LogE("[HttpResponseParser::onEof] EOF before headers complete: state={}",
                 stateName(state_));
            fail(slark::Error::ConnectionReset);
        }
        else if (state_ != State::Completed) {
            LogE("[HttpResponseParser::onEof] unexpected EOF: state={} bodyReceived={}",
                 stateName(state_), bodyReceived_);
            fail(slark::Error::ConnectionReset);
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

    [[nodiscard]] bool isHttpError() const noexcept {
        auto code = static_cast<uint16_t>(statusCode_);
        return code >= 400 && code < 600;
    }

    [[nodiscard]] bool shouldCloseConnection() const noexcept {
        return shouldClose_;
    }

    [[nodiscard]] bool canReuseConnection() const noexcept {
        return state_ == State::Completed && !isHttp10_ && !shouldClose_ && !bodyUsesEof_ &&
               statusCode_ != HttpStatusCode::SwitchingProtocols;
    }

    [[nodiscard]] HttpStatusCode statusCode() const noexcept {
        return statusCode_;
    }

    [[nodiscard]] bool isHttp10() const noexcept {
        return isHttp10_;
    }

    [[nodiscard]] bool isBodyUnknownLength() const noexcept {
        return bodyUsesEof_;
    }

    [[nodiscard]] bool endedByEof() const noexcept {
        return endedByEof_;
    }

    void resetForNextResponse() noexcept;

    [[nodiscard]] std::string_view unreadData() const noexcept {
        return std::string_view(buffer_).substr(readPos_);
    }

    [[nodiscard]] bool hasUnreadData() const noexcept {
        return readPos_ < buffer_.size();
    }

    [[nodiscard]] std::string takeUnreadData() noexcept;

    [[nodiscard]] const std::unordered_map<std::string, std::string>& headers() const noexcept {
        return headers_;
    }

    [[nodiscard]] std::unordered_map<std::string, std::string> takeHeaders() noexcept {
        return std::exchange(headers_, {});
    }

    // Idle lifetime the server advertised via "Keep-Alive: timeout=N"
    [[nodiscard]] std::optional<std::chrono::seconds> keepAliveTimeout() const noexcept;

    [[nodiscard]] const std::unordered_map<std::string, std::string>& trailers() const noexcept {
        return trailers_;
    }

    [[nodiscard]] std::unordered_map<std::string, std::string> takeTrailers() noexcept {
        return std::exchange(trailers_, {});
    }

    void reset() noexcept;

 private:
    void processCurrentState();
    bool checkHeaderOverflow(size_t appendSize) noexcept;

    void processStatusLine();
    void processHeaders();
    void processBodyFixed();
    void determineBodyMode();
    void processChunkSize();
    void processChunkData();
    void processChunkTrailer();

    static std::string_view stateName(State s) noexcept;

    [[nodiscard]] bool isInformationalResponse() const noexcept {
        const auto code = static_cast<uint16_t>(statusCode_);
        return code >= 100 && code < 200 && statusCode_ != HttpStatusCode::SwitchingProtocols;
    }

    void fail(Error code) noexcept {
        state_ = State::Error;
        if (callbacks_.onError) {
            callbacks_.onError(code);
        }
    }

    void complete() noexcept {
        state_ = State::Completed;
        if (callbacks_.onCompleted) {
            callbacks_.onCompleted();
        }
    }

    [[nodiscard]] bool shouldSkipBody() const noexcept {
        auto code = static_cast<uint16_t>(statusCode_);
        if (code >= 100 && code < 200) {
            return true;
        }
        if (statusCode_ == HttpStatusCode::NoContent ||
            statusCode_ == HttpStatusCode::ResetContent ||
            statusCode_ == HttpStatusCode::NotModified) {
            return true;
        }
        if (requestMethod_ == HttpMethodType::Head) {
            return true;
        }
        return false;
    }

    static constexpr std::string_view kCRLF = "\r\n";
    static constexpr std::string_view kHeaderEnd = "\r\n\r\n";
    static constexpr size_t kCompactionThreshold = 8192;  // fragmentation threshold
    static constexpr size_t kMaxTrailerSize = 16384;      // max cumulative trailer length
    static constexpr size_t kMaxChunkSizeLineLength = 256;  // chunk-size line incl. extensions
    // Logging-only sentinel for contentLength_ when the body length is unknown.
    static constexpr int64_t kUnknownContentLength = -1;

    State state_ = State::StatusLine;
    Callbacks callbacks_;
    std::string buffer_;
    size_t readPos_ = 0;  // logical consume position, avoids frequent erases
    std::unordered_map<std::string, std::string> headers_;
    std::unordered_map<std::string, std::string> trailers_;
    HttpStatusCode statusCode_ = HttpStatusCode::Unknown;
    bool isHttp10_ = false;
    bool shouldClose_ = false;
    HttpMethodType requestMethod_ = HttpMethodType::Unknown;

    std::optional<int64_t> contentLength_;
    std::optional<std::chrono::seconds> keepAliveTimeout_;
    std::string transferEncodingValue_;
    int64_t bodyReceived_ = 0;
    uint64_t maxResponseBodySize_ = kDefaultMaxResponseBodySize;
    int64_t currentChunkSize_ = 0;
    int64_t chunkBytesRead_ = 0;
    bool expectingChunkDataCrlf_ = false;
    size_t trailerBytesRead_ = 0;
    bool bodyUsesEof_ = false;
    bool endedByEof_ = false;

    RequestPerf* perf_ = nullptr;

    int64_t lastLoggedBodyReceived_ = 0;

 private:
    [[nodiscard]] std::string_view readableData() const noexcept {
        return std::string_view(buffer_).substr(readPos_);
    }

    void consume(size_t size) noexcept {
        readPos_ += size;
        if (readPos_ > kCompactionThreshold) {
            compact();
        }
    }

    void compact() noexcept {
        if (readPos_ > 0) {
            buffer_.erase(0, readPos_);
            readPos_ = 0;
        }
    }

    [[nodiscard]] size_t readableSize() const noexcept {
        return buffer_.size() - readPos_;
    }
};

}  // namespace slark::http
