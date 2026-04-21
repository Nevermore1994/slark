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
    // General errors(0 - 99)
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

    // File I / O errors(100 - 199) IOError = 100,
    EndOfFile = 101,
    PermissionDenied = 102,
    FileNotFound = 103,
    FileAlreadyExists = 104,

    // Decoder errors(200 - 299) DecoderNotFound = 200,
    DecoderError = 201,
    DecoderInputError = 202,
    DecoderInputTooLarge = 203,
    DecoderNotStarted = 204,
    DecoderNotProcessed = 205,
    DecoderAgain = 206,
    DecoderUnsupportedFormat = 207,
    DecoderResourceExhausted = 208,
    DecoderConfigInvalid = 209,

    // Stream errors(250 - 279) StreamNotReady = 250,
    StreamInvalidData = 251,

    // Network errors(300 - 399) NetworkError = 300,
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

    // HTTP errors(350 - 399) HttpError = 350,
    HttpTimeout = 351,
    HttpRedirectError = 352,
    HttpRedirectTooMany = 353,
    HttpResponseInvalid = 354,
    HttpChunkedError = 355,
    HttpSslError = 356,
    HttpSslHandshakeFailed = 357,
    HttpUrlInvalid = 358,
    HttpSchemeNotSupported = 359,

    // URL parse errors(370 - 389) UrlEmpty = 370,
    UrlMissingScheme = 371,
    UrlInvalidScheme = 372,
    UrlEmptyAuthority = 373,
    UrlInvalidHost = 374,
    UrlInvalidPort = 375,
    UrlPortOutOfRange = 376,
    UrlInvalidIPv6 = 377,

    // Demuxer errors(390 - 419) DemuxerFailed = 390,
    DemuxerFileEnd = 391,
    DemuxerParsedHeader = 392,
    DemuxerVideoInfoChange = 393,
    DemuxerParsedFPS = 394,
    DemuxerInvalidData = 395,

    // Player errors(400 - 499) PlayerFileError = 400,
    PlayerNetworkError = 401,
    FormatNotSupported = 402,
    DemuxError = 403,
    DecodeError = 404,
    RenderError = 405,
};

inline constexpr std::string_view toString(Error error) noexcept {
    switch (error) {
        case Error::Success:
            return "Success";
        case Error::OutOfMemory:
            return "Out of memory";
        case Error::OutOfRange:
            return "Out of range";
        case Error::NullPointer:
            return "Null pointer";
        case Error::BufferOverflow:
            return "Buffer overflow";
        case Error::InvalidOperation:
            return "Invalid operation";
        case Error::InvalidArgument:
            return "Invalid argument";
        case Error::SizeTooLarge:
            return "Size too large";
        case Error::EmptyData:
            return "Empty data";
        case Error::NotFound:
            return "Not found";
        case Error::Cancelled:
            return "Cancelled";
        case Error::IOError:
            return "I/O error";
        case Error::EndOfFile:
            return "End of file";
        case Error::PermissionDenied:
            return "Permission denied";
        case Error::FileNotFound:
            return "File not found";
        case Error::FileAlreadyExists:
            return "File already exists";
        case Error::DecoderNotFound:
            return "Decoder not found";
        case Error::DecoderError:
            return "Decoder error";
        case Error::DecoderInputError:
            return "Decoder input error";
        case Error::DecoderInputTooLarge:
            return "Decoder input too large";
        case Error::DecoderNotStarted:
            return "Decoder not started";
        case Error::DecoderNotProcessed:
            return "Decoder not processed";
        case Error::DecoderAgain:
            return "Decoder needs more input";
        case Error::DecoderUnsupportedFormat:
            return "Decoder unsupported format";
        case Error::DecoderResourceExhausted:
            return "Decoder resource exhausted";
        case Error::DecoderConfigInvalid:
            return "Decoder config invalid";
        case Error::StreamNotReady:
            return "Stream not ready";
        case Error::StreamInvalidData:
            return "Stream invalid data";
        case Error::NetworkError:
            return "Network error";
        case Error::ConnectionFailed:
            return "Connection failed";
        case Error::ConnectionTimeout:
            return "Connection timeout";
        case Error::ConnectionReset:
            return "Connection reset";
        case Error::ConnectionRefused:
            return "Connection refused";
        case Error::DnsResolveFailed:
            return "DNS resolve failed";
        case Error::SocketError:
            return "Socket error";
        case Error::SocketClosed:
            return "Socket closed";
        case Error::WouldBlock:
            return "Operation would block";
        case Error::InvalidSocket:
            return "Invalid socket";
        case Error::BrokenPipe:
            return "Broken pipe";
        case Error::HttpError:
            return "HTTP error";
        case Error::HttpTimeout:
            return "HTTP timeout";
        case Error::HttpRedirectError:
            return "HTTP redirect error";
        case Error::HttpRedirectTooMany:
            return "HTTP redirect too many";
        case Error::HttpResponseInvalid:
            return "HTTP response invalid";
        case Error::HttpChunkedError:
            return "HTTP chunked encoding error";
        case Error::HttpSslError:
            return "HTTP SSL error";
        case Error::HttpSslHandshakeFailed:
            return "HTTP SSL handshake failed";
        case Error::HttpUrlInvalid:
            return "HTTP URL invalid";
        case Error::HttpSchemeNotSupported:
            return "HTTP scheme not supported";
        case Error::UrlEmpty:
            return "URL empty";
        case Error::UrlMissingScheme:
            return "URL missing scheme";
        case Error::UrlInvalidScheme:
            return "URL invalid scheme";
        case Error::UrlEmptyAuthority:
            return "URL empty authority";
        case Error::UrlInvalidHost:
            return "URL invalid host";
        case Error::UrlInvalidPort:
            return "URL invalid port";
        case Error::UrlPortOutOfRange:
            return "URL port out of range";
        case Error::UrlInvalidIPv6:
            return "URL invalid IPv6";
        case Error::DemuxerFailed:
            return "Demuxer failed";
        case Error::DemuxerFileEnd:
            return "Demuxer file end";
        case Error::DemuxerParsedHeader:
            return "Demuxer parsed header";
        case Error::DemuxerVideoInfoChange:
            return "Demuxer video info change";
        case Error::DemuxerParsedFPS:
            return "Demuxer parsed FPS";
        case Error::DemuxerInvalidData:
            return "Demuxer invalid data";
        case Error::PlayerFileError:
            return "Player file error";
        case Error::PlayerNetworkError:
            return "Player network error";
        case Error::FormatNotSupported:
            return "Format not supported";
        case Error::DemuxError:
            return "Demux error";
        case Error::DecodeError:
            return "Decode error";
        case Error::RenderError:
            return "Render error";
        default:
            return "Unknown error";
    }
}

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

} // namespace slark
