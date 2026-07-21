//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//
#include "Error.h"

namespace slark {

std::string_view toString(Error error) noexcept {
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
        case Error::StreamNotReady:
            return "Stream not ready";
        case Error::StreamInvalidData:
            return "Stream invalid data";
        case Error::CodecNotFound:
            return "Codec not found";
        case Error::CodecError:
            return "Codec error";
        case Error::CodecInputError:
            return "Codec input error";
        case Error::CodecInputTooLarge:
            return "Codec input too large";
        case Error::CodecNotStarted:
            return "Codec not started";
        case Error::CodecNotProcessed:
            return "Codec not processed";
        case Error::CodecAgain:
            return "Codec needs more input";
        case Error::CodecUnsupportedFormat:
            return "Codec unsupported format";
        case Error::CodecResourceExhausted:
            return "Codec resource exhausted";
        case Error::CodecConfigInvalid:
            return "Codec config invalid";
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
        case Error::MediaOpenFailed:
            return "Media open failed";
        case Error::MediaEndOfFile:
            return "Media end of file";
        case Error::MediaHeaderParsed:
            return "Media header parsed";
        case Error::MediaInfoChanged:
            return "Media info changed";
        case Error::MediaFpsParsed:
            return "Media FPS parsed";
        case Error::MediaInvalidData:
            return "Media invalid data";
        case Error::PipelineOpenFailed:
            return "Pipeline open failed";
        case Error::PipelineFormatNotSupported:
            return "Pipeline format not supported";
        case Error::PipelineDemuxError:
            return "Pipeline demux error";
        case Error::PipelineDecodeError:
            return "Pipeline decode error";
        case Error::PipelineRenderError:
            return "Pipeline render error";
        case Error::Failed:
            return "Generic failure";
        case Error::SchemeNotSupported:
            return "Scheme not supported";
        case Error::SchemeDowngradeBlocked:
            return "Scheme downgrade blocked";
        default:
            return "Unknown error";
    }
}

} // namespace slark
