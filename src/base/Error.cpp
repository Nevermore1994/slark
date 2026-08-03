//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//
#include "Error.h"

#include <unordered_map>

namespace slark {

std::string_view toString(Error error) noexcept {
    using namespace std::string_view_literals;
    static const std::unordered_map<Error, std::string_view> kErrorNames = {
        {Error::Success, "Success"},
        {Error::OutOfMemory, "Out of memory"},
        {Error::OutOfRange, "Out of range"},
        {Error::NullPointer, "Null pointer"},
        {Error::BufferOverflow, "Buffer overflow"},
        {Error::InvalidOperation, "Invalid operation"},
        {Error::InvalidArgument, "Invalid argument"},
        {Error::SizeTooLarge, "Size too large"},
        {Error::EmptyData, "Empty data"},
        {Error::NotFound, "Not found"},
        {Error::Cancelled, "Cancelled"},
        {Error::IOError, "I/O error"},
        {Error::EndOfFile, "End of file"},
        {Error::PermissionDenied, "Permission denied"},
        {Error::FileNotFound, "File not found"},
        {Error::FileAlreadyExists, "File already exists"},
        {Error::StreamNotReady, "Stream not ready"},
        {Error::StreamInvalidData, "Stream invalid data"},
        {Error::CodecNotFound, "Codec not found"},
        {Error::CodecError, "Codec error"},
        {Error::CodecInputError, "Codec input error"},
        {Error::CodecInputTooLarge, "Codec input too large"},
        {Error::CodecNotStarted, "Codec not started"},
        {Error::CodecNotProcessed, "Codec not processed"},
        {Error::CodecAgain, "Codec needs more input"},
        {Error::CodecUnsupportedFormat, "Codec unsupported format"},
        {Error::CodecResourceExhausted, "Codec resource exhausted"},
        {Error::CodecConfigInvalid, "Codec config invalid"},
        {Error::NetworkError, "Network error"},
        {Error::ConnectionFailed, "Connection failed"},
        {Error::ConnectionTimeout, "Connection timeout"},
        {Error::ConnectionReset, "Connection reset"},
        {Error::ConnectionRefused, "Connection refused"},
        {Error::DnsResolveFailed, "DNS resolve failed"},
        {Error::SocketError, "Socket error"},
        {Error::SocketClosed, "Socket closed"},
        {Error::WouldBlock, "Operation would block"},
        {Error::InvalidSocket, "Invalid socket"},
        {Error::BrokenPipe, "Broken pipe"},
        {Error::HttpError, "HTTP error"},
        {Error::HttpTimeout, "HTTP timeout"},
        {Error::HttpRedirectError, "HTTP redirect error"},
        {Error::HttpRedirectTooMany, "HTTP redirect too many"},
        {Error::HttpResponseInvalid, "HTTP response invalid"},
        {Error::HttpChunkedError, "HTTP chunked encoding error"},
        {Error::HttpSslError, "HTTP SSL error"},
        {Error::HttpSslHandshakeFailed, "HTTP SSL handshake failed"},
        {Error::MediaOpenFailed, "Media open failed"},
        {Error::MediaEndOfFile, "Media end of file"},
        {Error::MediaHeaderParsed, "Media header parsed"},
        {Error::MediaInfoChanged, "Media info changed"},
        {Error::MediaFpsParsed, "Media FPS parsed"},
        {Error::MediaInvalidData, "Media invalid data"},
        {Error::PipelineOpenFailed, "Pipeline open failed"},
        {Error::PipelineFormatNotSupported, "Pipeline format not supported"},
        {Error::PipelineDemuxError, "Pipeline demux error"},
        {Error::PipelineDecodeError, "Pipeline decode error"},
        {Error::PipelineRenderError, "Pipeline render error"},
        {Error::Failed, "Generic failure"},
        {Error::SchemeNotSupported, "Scheme not supported"},
        {Error::SchemeDowngradeBlocked, "Scheme downgrade blocked"},
    };
    if (auto it = kErrorNames.find(error); it != kErrorNames.end()) {
        return it->second;
    }
    return "Unknown error"sv;
}

}  // namespace slark
