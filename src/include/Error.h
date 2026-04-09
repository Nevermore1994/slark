//
//  Error.h
//  slark
//
//  Created by Nevermore on 2025/3/31.

#pragma once

#include <expected>
#include <stdexcept>
#include <string>
#include <string_view>

namespace slark {

enum class DataError : uint16_t {
    // General errors (0-99)
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

    // File I/O errors (100-199)
    IOError = 100,
    EndOfFile = 101,
    PermissionDenied = 102,
    FileNotFound = 103,
    FileAlreadyExists = 104,

    // Decoder errors (200-299)
    DecoderNotFound = 200,
    DecoderError = 201,
    DecoderInputError = 202,
    DecoderInputTooLarge = 203,
    DecoderNotStarted = 204,
    DecoderNotProcessed = 205,
    DecoderAgain = 206,

    // Network errors (300-399)
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

    // HTTP errors (350-399)
    HttpError = 350,
    HttpTimeout = 351,
    HttpRedirectError = 352,
    HttpRedirectTooMany = 353,
    HttpResponseInvalid = 354,
    HttpChunkedError = 355,
    HttpSslError = 356,
    HttpSslHandshakeFailed = 357,
    HttpUrlInvalid = 358,
    HttpSchemeNotSupported = 359,

    // Player errors (400-499)
    PlayerFileError = 400,
    PlayerNetworkError = 401,
    FormatNotSupported = 402,
    DemuxError = 403,
    DecodeError = 404,
    RenderError = 405,
};

inline constexpr std::string_view toString(DataError error) noexcept {
    switch (error) {
        case DataError::Success:
            return "Success";
        case DataError::OutOfMemory:
            return "Out of memory";
        case DataError::OutOfRange:
            return "Out of range";
        case DataError::NullPointer:
            return "Null pointer";
        case DataError::BufferOverflow:
            return "Buffer overflow";
        case DataError::InvalidOperation:
            return "Invalid operation";
        case DataError::InvalidArgument:
            return "Invalid argument";
        case DataError::SizeTooLarge:
            return "Size too large";
        case DataError::EmptyData:
            return "Empty data";
        case DataError::NotFound:
            return "Not found";
        case DataError::Cancelled:
            return "Cancelled";
        case DataError::IOError:
            return "I/O error";
        case DataError::EndOfFile:
            return "End of file";
        case DataError::PermissionDenied:
            return "Permission denied";
        case DataError::FileNotFound:
            return "File not found";
        case DataError::FileAlreadyExists:
            return "File already exists";
        case DataError::DecoderNotFound:
            return "Decoder not found";
        case DataError::DecoderError:
            return "Decoder error";
        case DataError::DecoderInputError:
            return "Decoder input error";
        case DataError::DecoderInputTooLarge:
            return "Decoder input too large";
        case DataError::DecoderNotStarted:
            return "Decoder not started";
        case DataError::DecoderNotProcessed:
            return "Decoder not processed";
        case DataError::DecoderAgain:
            return "Decoder needs more input";
        case DataError::NetworkError:
            return "Network error";
        case DataError::ConnectionFailed:
            return "Connection failed";
        case DataError::ConnectionTimeout:
            return "Connection timeout";
        case DataError::ConnectionReset:
            return "Connection reset";
        case DataError::ConnectionRefused:
            return "Connection refused";
        case DataError::DnsResolveFailed:
            return "DNS resolve failed";
        case DataError::SocketError:
            return "Socket error";
        case DataError::SocketClosed:
            return "Socket closed";
        case DataError::WouldBlock:
            return "Operation would block";
        case DataError::InvalidSocket:
            return "Invalid socket";
        case DataError::BrokenPipe:
            return "Broken pipe";
        case DataError::HttpError:
            return "HTTP error";
        case DataError::HttpTimeout:
            return "HTTP timeout";
        case DataError::HttpRedirectError:
            return "HTTP redirect error";
        case DataError::HttpRedirectTooMany:
            return "HTTP redirect too many";
        case DataError::HttpResponseInvalid:
            return "HTTP response invalid";
        case DataError::HttpChunkedError:
            return "HTTP chunked encoding error";
        case DataError::HttpSslError:
            return "HTTP SSL error";
        case DataError::HttpSslHandshakeFailed:
            return "HTTP SSL handshake failed";
        case DataError::HttpUrlInvalid:
            return "HTTP URL invalid";
        case DataError::HttpSchemeNotSupported:
            return "HTTP scheme not supported";
        case DataError::PlayerFileError:
            return "Player file error";
        case DataError::PlayerNetworkError:
            return "Player network error";
        case DataError::FormatNotSupported:
            return "Format not supported";
        case DataError::DemuxError:
            return "Demux error";
        case DataError::DecodeError:
            return "Decode error";
        case DataError::RenderError:
            return "Render error";
        default:
            return "Unknown error";
    }
}

template <typename T>
using Expected = std::expected<T, DataError>;

template <typename T>
inline constexpr Expected<T> makeUnexpected(DataError error) noexcept {
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

// 超时异常：用于精确区分超时与其他错误，避免字符串匹配的不确定性
class TimeoutException : public std::runtime_error {
public:
    explicit TimeoutException(std::string_view message = "Timeout")
        : std::runtime_error(std::string(message)) {}
};

} // namespace slark
