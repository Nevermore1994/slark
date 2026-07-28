//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#pragma once

#include <expected>
#include <stdexcept>
#include <string>
#include <string_view>

namespace slark {

enum class Error : uint16_t {
    Success = 0,
    OutOfMemory = 1,
    OutOfRange = 2,
    NullPointer = 3,
    BufferOverflow = 4,
    InvalidOperation = 5,
    InvalidArgument = 6,
    SizeTooLarge = 7,
    EmptyData = 8,
    NotFound = 9,
    Cancelled = 10,

    IOError = 100,
    EndOfFile = 101,
    PermissionDenied = 102,
    FileNotFound = 103,
    FileAlreadyExists = 104,
    StreamNotReady = 110,
    StreamInvalidData = 111,

    CodecNotFound = 200,
    CodecError = 201,
    CodecInputError = 202,
    CodecInputTooLarge = 203,
    CodecNotStarted = 204,
    CodecNotProcessed = 205,
    CodecAgain = 206,
    CodecUnsupportedFormat = 207,
    CodecResourceExhausted = 208,
    CodecConfigInvalid = 209,

    NetworkError = 300,
    ConnectionFailed = 301,
    ConnectionTimeout = 302,
    ConnectionReset = 303,
    ConnectionRefused = 304,
    DnsResolveFailed = 305,
    SocketError = 306,
    SocketClosed = 307,
    WouldBlock = 308,
    InvalidSocket = 309,
    BrokenPipe = 310,

    HttpError = 400,
    HttpTimeout = 401,
    HttpRedirectError = 402,
    HttpRedirectTooMany = 403,
    HttpResponseInvalid = 404,
    HttpChunkedError = 405,
    HttpSslError = 406,
    HttpSslHandshakeFailed = 407,

    MediaOpenFailed = 500,
    MediaEndOfFile = 501,
    MediaHeaderParsed = 502,
    MediaInfoChanged = 503,
    MediaFpsParsed = 504,
    MediaInvalidData = 505,

    PipelineOpenFailed = 600,
    PipelineFormatNotSupported = 601,
    PipelineDemuxError = 602,
    PipelineDecodeError = 603,
    PipelineRenderError = 604,

    Failed = 700,
    SchemeNotSupported = 712,
    SchemeDowngradeBlocked = 719,
};

std::string_view toString(Error error) noexcept;

template <typename T>
using Expected = std::expected<T, Error>;

template <typename T>
inline constexpr Expected<T> makeUnexpected(Error error) noexcept {
    return std::unexpected(error);
}

template <typename T>
inline constexpr bool isSuccess(const Expected<T>& result) noexcept {
    return result.has_value();
}

template <typename T>
inline constexpr T valueOr(const Expected<T>& result, T defaultValue) noexcept {
    return result.value_or(std::move(defaultValue));
}

class TimeoutException : public std::runtime_error {
 public:
    explicit TimeoutException(std::string_view message = "Timeout")
        : std::runtime_error(std::string(message)) {}
};

}  // namespace slark
