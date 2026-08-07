//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>

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
        std::function<void(std::unordered_map<std::string, std::string>&&)> onHeaders;
        std::function<void(const Byte*, size_t)> onBodyData;
        std::function<void()> onCompleted;
        std::function<void(Error)> onError;
    };

    explicit HttpResponseParser(Callbacks callbacks) : callbacks_(std::move(callbacks)) {}

    // Set the request method, used to determine that HEAD responses have no body
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

    // Finalization when EOF arrives.
    // For responses without Content-Length that are not chunked, EOF marks the end of message.
    void onEof() {
        if (state_ == State::Completed || state_ == State::Error) {
            return;
        }

        LogI("[HttpResponseParser::onEof] state={} bodyReceived={} contentLength={}",
             stateName(state_), bodyReceived_, contentLength_);

        // Only a body with unknown length uses EOF as its end marker
        if (state_ == State::BodyFixed && contentLength_ < 0) {
            complete();
        } else if (state_ == State::StatusLine || state_ == State::Headers) {
            // Connection dropped before headers were fully received
            LogE("[HttpResponseParser::onEof] EOF before headers complete: state={}",
                 stateName(state_));
            fail(slark::Error::ConnectionReset);
        }
        // Other states (e.g. chunked dropped mid-stream) are treated as errors
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

    // 4xx/5xx error responses: the caller should stop reading the body immediately
    // to avoid wasted bandwidth and latency
    [[nodiscard]] bool isHttpError() const noexcept {
        auto code = static_cast<uint16_t>(statusCode_);
        return code >= 400 && code < 600;
    }

    [[nodiscard]] bool shouldCloseConnection() const noexcept {
        return shouldClose_;
    }

    [[nodiscard]] bool isHttp10() const noexcept {
        return isHttp10_;
    }

    [[nodiscard]] bool isBodyUnknownLength() const noexcept {
        return state_ == State::BodyFixed && contentLength_ < 0;
    }

    [[nodiscard]] const std::unordered_map<std::string, std::string>& headers() const noexcept {
        return headers_;
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

    // Unified failure exit to avoid duplicated state changes and callbacks
    void fail(Error code) noexcept {
        state_ = State::Error;
        if (callbacks_.onError) {
            callbacks_.onError(code);
        }
    }

    // Unified completion exit
    void complete() noexcept {
        state_ = State::Completed;
        if (callbacks_.onCompleted) {
            callbacks_.onCompleted();
        }
    }

    // RFC 7230: determine whether the response should skip its body
    [[nodiscard]] bool shouldSkipBody() const noexcept {
        // 1xx informational responses have no body
        auto code = static_cast<uint16_t>(statusCode_);
        if (code >= 100 && code < 200) {
            return true;
        }
        // 204 No Content and 304 Not Modified have no body
        if (statusCode_ == HttpStatusCode::NoContent ||
            statusCode_ == HttpStatusCode::ResetContent ||
            statusCode_ == HttpStatusCode::NotModified) {
            return true;
        }
        // HEAD request responses have no body
        if (requestMethod_ == HttpMethodType::Head) {
            return true;
        }
        return false;
    }

    static constexpr std::string_view kCRLF = "\r\n";
    static constexpr std::string_view kHeaderEnd = "\r\n\r\n";
    static constexpr size_t kCompactionThreshold = 8192;  // fragmentation threshold
    static constexpr size_t kMaxTrailerSize = 16384;      // max cumulative trailer length

    State state_ = State::StatusLine;
    Callbacks callbacks_;
    std::string buffer_;
    size_t readPos_ = 0;  // logical consume position, avoids frequent erases
    std::unordered_map<std::string, std::string> headers_;
    HttpStatusCode statusCode_ = HttpStatusCode::Unknown;
    bool isHttp10_ = false;
    bool shouldClose_ = false;
    bool connectionCloseSeen_ = false;
    bool connectionKeepAliveSeen_ = false;
    HttpMethodType requestMethod_ = HttpMethodType::Unknown;

    int64_t contentLength_ = -1;
    bool contentLengthSeen_ = false;
    bool transferEncodingSeen_ = false;
    std::string transferEncodingValue_;
    int64_t bodyReceived_ = 0;
    uint64_t maxResponseBodySize_ = kDefaultMaxResponseBodySize;
    int64_t currentChunkSize_ = 0;
    int64_t chunkBytesRead_ = 0;
    bool expectingChunkDataCrlf_ = false;
    size_t trailerBytesRead_ = 0;

    RequestPerf* perf_ = nullptr;

    // Protocol-level log throttling: last logged cumulative bodyReceived_
    int64_t lastLoggedBodyReceived_ = 0;

 private:
    [[nodiscard]] std::string_view readableData() const noexcept {
        return std::string_view(buffer_).substr(readPos_);
    }

    void consume(size_t size) noexcept {
        readPos_ += size;
        // Compact when fragmentation exceeds the threshold
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
