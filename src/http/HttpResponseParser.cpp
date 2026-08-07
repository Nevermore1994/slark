//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//
#include "HttpResponseParser.h"

#include <algorithm>
#include <charconv>
#include <cctype>

#include "DataView.hpp"
#include "Error.h"
#include "Log.hpp"
#include "LogConfig.h"
#include "StringUtil.h"
#include "Time.hpp"

namespace slark::http {

namespace {

inline int64_t parserLogChunkBytes() {
    return static_cast<int64_t>(getLogConfig().progressLogBytes);
}

bool parseNonNegativeInt64(std::string_view text, int base, int64_t& value) noexcept {
    if (text.empty() || base < 2 || base > 36 || text.front() == '-' || text.front() == '+') {
        return false;
    }

    int64_t parsed = 0;
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), parsed, base);
    if (error != std::errc() || end != text.data() + text.size() || parsed < 0) {
        return false;
    }

    value = parsed;
    return true;
}

bool isHeaderNameToken(std::string_view name) noexcept {
    if (name.empty()) {
        return false;
    }
    return std::all_of(name.begin(), name.end(), [](unsigned char c) {
        return std::isalnum(c) || c == '!' || c == '#' || c == '$' || c == '%' || c == '&' ||
               c == '\'' || c == '*' || c == '+' || c == '-' || c == '.' || c == '^' || c == '_' ||
               c == '`' || c == '|' || c == '~';
    });
}

bool isHeaderValueValid(std::string_view value) noexcept {
    return std::all_of(value.begin(), value.end(), [](unsigned char c) {
        return c == '\t' || (c >= 0x20 && c != 0x7f);
    });
}

std::string_view trimOws(std::string_view value) noexcept {
    while (!value.empty() && (value.front() == ' ' || value.front() == '\t')) {
        value.remove_prefix(1);
    }
    while (!value.empty() && (value.back() == ' ' || value.back() == '\t')) {
        value.remove_suffix(1);
    }
    return value;
}

bool hasHeaderToken(std::string_view value, std::string_view target) noexcept {
    while (true) {
        const auto comma = value.find(',');
        const auto token = trimOws(value.substr(0, comma));
        if (!token.empty() && StringUtil::iequals(token, target)) {
            return true;
        }
        if (comma == std::string_view::npos) {
            return false;
        }
        value.remove_prefix(comma + 1);
    }
}

bool isSingleChunkedTransferEncoding(std::string_view value) noexcept {
    bool foundChunked = false;
    while (true) {
        const auto comma = value.find(',');
        const auto token = trimOws(value.substr(0, comma));
        if (token.empty() || !StringUtil::iequals(token, "chunked") || foundChunked) {
            return false;
        }
        foundChunked = true;
        if (comma == std::string_view::npos) {
            return true;
        }
        value.remove_prefix(comma + 1);
    }
}

}  // namespace

bool HttpResponseParser::checkHeaderOverflow(size_t appendSize) noexcept {
    constexpr size_t kMaxHeaderBufferSize = 64 * 1024;
    if (state_ != State::StatusLine && state_ != State::Headers) {
        return false;
    }
    if (buffer_.size() + appendSize > kMaxHeaderBufferSize) {
        LogE("[HttpResponseParser::feed] header buffer overflow: size={} state={}",
             buffer_.size() + appendSize, stateName(state_));
        fail(slark::Error::HttpResponseInvalid);
        return true;
    }
    return false;
}

void HttpResponseParser::processCurrentState() {
    switch (state_) {
        case State::StatusLine:    processStatusLine(); break;
        case State::Headers:       processHeaders(); break;
        case State::BodyFixed:     processBodyFixed(); break;
        case State::BodyChunked:
        case State::ChunkSize:     processChunkSize(); break;
        case State::ChunkData:     processChunkData(); break;
        case State::ChunkTrailer:  processChunkTrailer(); break;
        default: break;
    }
}

void HttpResponseParser::feed(const Byte* data, size_t size) {
    if (state_ == State::Completed || state_ == State::Error) {
        return;
    }

    auto parseStart = Time::nowTimeStamp();

    if (checkHeaderOverflow(size)) {
        return;
    }

    buffer_.append(reinterpret_cast<const char*>(data), size);

    while (state_ != State::Completed && state_ != State::Error) {
        auto prevSize = readableSize();
        processCurrentState();

        if (readableSize() == prevSize && state_ != State::Completed && state_ != State::Error) {
            break;
        }
    }

    if (perf_) {
        auto parseEnd = Time::nowTimeStamp();
        perf_->parser.headerParseTimeUs += (parseEnd - parseStart).point();
    }
}

void HttpResponseParser::reset() noexcept {
    state_ = State::StatusLine;
    buffer_.clear();
    readPos_ = 0;
    headers_.clear();
    statusCode_ = HttpStatusCode::Unknown;
    isHttp10_ = false;
    shouldClose_ = false;
    connectionCloseSeen_ = false;
    connectionKeepAliveSeen_ = false;
    contentLength_ = -1;
    contentLengthSeen_ = false;
    transferEncodingSeen_ = false;
    transferEncodingValue_.clear();
    bodyReceived_ = 0;
    currentChunkSize_ = 0;
    chunkBytesRead_ = 0;
    expectingChunkDataCrlf_ = false;
    trailerBytesRead_ = 0;
    lastLoggedBodyReceived_ = 0;
}

std::string_view HttpResponseParser::stateName(State s) noexcept {
    switch (s) {
        case State::StatusLine:
            return "StatusLine";
        case State::Headers:
            return "Headers";
        case State::BodyFixed:
            return "BodyFixed";
        case State::BodyChunked:
            return "BodyChunked";
        case State::ChunkSize:
            return "ChunkSize";
        case State::ChunkData:
            return "ChunkData";
        case State::ChunkTrailer:
            return "ChunkTrailer";
        case State::Completed:
            return "Completed";
        case State::Error:
            return "Error";
    }
    return "Unknown";
}

void HttpResponseParser::processStatusLine() {
    auto data = readableData();
    auto pos = data.find(kCRLF);
    if (pos == std::string_view::npos) {
        return;
    }

    auto line = data.substr(0, pos);
    constexpr std::string_view kHttpPrefix = "HTTP/";
    if (!line.starts_with(kHttpPrefix)) {
        LogE("[HttpResponseParser::processStatusLine] invalid HTTP/ prefix, line={}",
             StringUtil::toSafeLogString(line));
        fail(slark::Error::HttpResponseInvalid);
        return;
    }

    const auto firstSpace = line.find(' ', kHttpPrefix.size());
    if (firstSpace == std::string_view::npos) {
        LogE("[HttpResponseParser::processStatusLine] malformed status line, line={}",
             StringUtil::toSafeLogString(line));
        fail(slark::Error::HttpResponseInvalid);
        return;
    }

    const auto version = line.substr(kHttpPrefix.size(), firstSpace - kHttpPrefix.size());
    if (version != "1.0" && version != "1.1") {
        LogE("[HttpResponseParser::processStatusLine] unsupported HTTP version: {}", version);
        fail(slark::Error::HttpResponseInvalid);
        return;
    }
    isHttp10_ = version == "1.0";
    shouldClose_ = isHttp10_;

    const auto afterVersion = line.substr(firstSpace + 1);
    const auto secondSpace = afterVersion.find(' ');
    if (secondSpace == std::string_view::npos) {
        LogE("[HttpResponseParser::processStatusLine] missing reason phrase, line={}",
             StringUtil::toSafeLogString(line));
        fail(slark::Error::HttpResponseInvalid);
        return;
    }

    const auto statusCodeStr = afterVersion.substr(0, secondSpace);
    const auto reasonPhrase = afterVersion.substr(secondSpace + 1);
    if (statusCodeStr.size() != 3 ||
        !std::all_of(statusCodeStr.begin(), statusCodeStr.end(), [](unsigned char c) {
            return std::isdigit(c);
        })) {
        LogE("[HttpResponseParser::processStatusLine] invalid status code: {}",
             StringUtil::toSafeLogString(statusCodeStr));
        fail(slark::Error::HttpResponseInvalid);
        return;
    }

    auto codeResult = StringUtil::fromChars<int32_t>(statusCodeStr);
    if (!codeResult) {
        LogE("[HttpResponseParser::processStatusLine] invalid status code: {}",
             StringUtil::toSafeLogString(statusCodeStr));
        fail(slark::Error::HttpResponseInvalid);
        return;
    }

    statusCode_ = static_cast<HttpStatusCode>(*codeResult);
    consume(pos + kCRLF.size());
    state_ = State::Headers;

    LogI("[HttpResponseParser::processStatusLine] version={} status={} reason={} -> {}",
         version, static_cast<int>(statusCode_), std::string(reasonPhrase), stateName(state_));

    if (callbacks_.onStatusLine) {
        callbacks_.onStatusLine(statusCode_, std::string(reasonPhrase));
    }
}

void HttpResponseParser::determineBodyMode() {
    if (shouldSkipBody()) {
        LogI("[HttpResponseParser::processHeaders] headers done: count={} contentLength={} "
             "shouldSkipBody=true -> Completed",
             headers_.size(), contentLength_);
        state_ = State::Completed;
        return;
    }

    if (contentLength_ >= 0 &&
        static_cast<uint64_t>(contentLength_) > maxResponseBodySize_) {
        LogE("[HttpResponseParser::processHeaders] response body exceeds limit: length={} limit={}",
             contentLength_, maxResponseBodySize_);
        fail(slark::Error::HttpResponseBodyTooLarge);
        return;
    }

    if (transferEncodingSeen_ && contentLengthSeen_) {
        LogE("[HttpResponseParser::processHeaders] both Transfer-Encoding and Content-Length present");
        fail(slark::Error::HttpResponseInvalid);
        return;
    }

    if (transferEncodingSeen_) {
        if (!isSingleChunkedTransferEncoding(transferEncodingValue_)) {
            LogE("[HttpResponseParser::processHeaders] unsupported Transfer-Encoding: {}",
                 transferEncodingValue_);
            fail(slark::Error::HttpResponseInvalid);
            return;
        }
        state_ = State::ChunkSize;
        LogI("[HttpResponseParser::processHeaders] headers done: count={} transferEncoding=chunked "
             "shouldClose={} -> {}",
             headers_.size(), shouldClose_, stateName(state_));
        return;
    }

    if (contentLength_ == 0) {
        LogI("[HttpResponseParser::processHeaders] headers done: count={} contentLength=0 -> "
             "Completed",
             headers_.size());
        state_ = State::Completed;
    } else {
        state_ = State::BodyFixed;
        LogI("[HttpResponseParser::processHeaders] headers done: count={} contentLength={} "
             "shouldClose={} -> BodyFixed",
             headers_.size(), contentLength_, shouldClose_);
    }
}

void HttpResponseParser::processHeaders() {
    auto data = readableData();
    size_t pos = 0;
    size_t headerEndSize = kHeaderEnd.size();
    if (data.starts_with(kCRLF)) {
        headerEndSize = kCRLF.size();
    } else {
        const auto headerEndPos = data.find(kHeaderEnd);
        if (headerEndPos == std::string_view::npos) {
            return;
        }
        pos = headerEndPos;
    }

    auto headerBlock = data.substr(0, pos);

    size_t lineStart = 0;
    while (lineStart < headerBlock.size()) {
        auto lineEnd = headerBlock.find(kCRLF, lineStart);
        if (lineEnd == std::string_view::npos) {
            lineEnd = headerBlock.size();
        }

        auto line = headerBlock.substr(lineStart, lineEnd - lineStart);
        const auto colonPos = line.find(':');
        if (colonPos == std::string_view::npos) {
            LogE("[HttpResponseParser::processHeaders] header line has no colon: {}",
                 StringUtil::toSafeLogString(line));
            fail(slark::Error::HttpResponseInvalid);
            return;
        }

        auto name = StringUtil::toLower(std::string(line.substr(0, colonPos)));
        if (!isHeaderNameToken(name)) {
            LogE("[HttpResponseParser::processHeaders] invalid header name: {}",
                 StringUtil::toSafeLogString(name));
            fail(slark::Error::HttpResponseInvalid);
            return;
        }

        auto value = trimOws(line.substr(colonPos + 1));
        if (!isHeaderValueValid(value)) {
            LogE("[HttpResponseParser::processHeaders] invalid header value: {}",
                 StringUtil::toSafeLogString(value));
            fail(slark::Error::HttpResponseInvalid);
            return;
        }

        if (name == "content-length") {
            int64_t parsedLength = 0;
            if (!parseNonNegativeInt64(value, 10, parsedLength)) {
                LogE("[HttpResponseParser::processHeaders] invalid Content-Length: {}",
                     std::string(value));
                fail(slark::Error::HttpResponseInvalid);
                return;
            }
            if (contentLengthSeen_ && contentLength_ != parsedLength) {
                LogE("[HttpResponseParser::processHeaders] conflicting Content-Length values: {} and {}",
                     contentLength_, parsedLength);
                fail(slark::Error::HttpResponseInvalid);
                return;
            }
            contentLength_ = parsedLength;
            contentLengthSeen_ = true;
        } else if (name == "transfer-encoding") {
            if (transferEncodingSeen_) {
                transferEncodingValue_.append(",");
            }
            transferEncodingValue_.append(value);
            transferEncodingSeen_ = true;
        } else if (name == "connection") {
            connectionCloseSeen_ = connectionCloseSeen_ || hasHeaderToken(value, "close");
            connectionKeepAliveSeen_ =
                connectionKeepAliveSeen_ || hasHeaderToken(value, "keep-alive");
        }
        headers_[std::move(name)] = std::string(value);

        lineStart = lineEnd + kCRLF.size();
    }

    consume(pos + headerEndSize);
    shouldClose_ = connectionCloseSeen_ || (isHttp10_ && !connectionKeepAliveSeen_);

    if (isInformationalResponse()) {
        headers_.clear();
        contentLength_ = -1;
        contentLengthSeen_ = false;
        transferEncodingSeen_ = false;
        transferEncodingValue_.clear();
        connectionCloseSeen_ = false;
        connectionKeepAliveSeen_ = false;
        shouldClose_ = isHttp10_;
        bodyReceived_ = 0;
        currentChunkSize_ = 0;
        chunkBytesRead_ = 0;
        expectingChunkDataCrlf_ = false;
        state_ = State::StatusLine;
        return;
    }

    determineBodyMode();
    if (state_ == State::Error) {
        return;
    }

    if (callbacks_.onHeaders) {
        callbacks_.onHeaders(std::unordered_map<std::string, std::string>(headers_));
    }
    if (state_ == State::Completed) {
        complete();
    }
}

void HttpResponseParser::processBodyFixed() {
    auto data = readableData();
    if (data.empty()) {
        return;
    }

    auto available = static_cast<int64_t>(data.size());

    if (contentLength_ > 0) {
        auto remaining = contentLength_ - bodyReceived_;
        auto toConsume = std::min(available, remaining);
        if (toConsume > 0 &&
            static_cast<uint64_t>(bodyReceived_) + static_cast<uint64_t>(toConsume) >
                maxResponseBodySize_) {
            LogE("[HttpResponseParser::processBodyFixed] response body exceeds limit: received={} "
                 "next={} limit={}",
                 bodyReceived_, toConsume, maxResponseBodySize_);
            fail(slark::Error::HttpResponseBodyTooLarge);
            return;
        }

        if (toConsume > 0 && callbacks_.onBodyData) {
            callbacks_.onBodyData(reinterpret_cast<const Byte*>(data.data()),
                                  static_cast<size_t>(toConsume));
        }

        bodyReceived_ += toConsume;
        consume(static_cast<size_t>(toConsume));

        if (bodyReceived_ >= contentLength_) {
            LogI("[HttpResponseParser::processBodyFixed] complete: bodyReceived={}/{}",
                 bodyReceived_, contentLength_);
            complete();
        } else if (bodyReceived_ - lastLoggedBodyReceived_ >= parserLogChunkBytes()) {
            LogI("[HttpResponseParser::processBodyFixed] progress: bodyReceived={}/{}",
                 bodyReceived_, contentLength_);
            lastLoggedBodyReceived_ = bodyReceived_;
        }
    } else {
        // Content-Length unknown, consume all available data
        if (static_cast<uint64_t>(bodyReceived_) + static_cast<uint64_t>(available) >
            maxResponseBodySize_) {
            LogE("[HttpResponseParser::processBodyFixed] response body exceeds limit: received={} "
                 "available={} limit={}",
                 bodyReceived_, available, maxResponseBodySize_);
            fail(slark::Error::HttpResponseBodyTooLarge);
            return;
        }
        if (callbacks_.onBodyData) {
            callbacks_.onBodyData(reinterpret_cast<const Byte*>(data.data()), data.size());
        }
        bodyReceived_ += available;
        consume(data.size());

        if (bodyReceived_ - lastLoggedBodyReceived_ >= parserLogChunkBytes()) {
            LogI(
                "[HttpResponseParser::processBodyFixed] progress (unknown length): bodyReceived={}",
                bodyReceived_);
            lastLoggedBodyReceived_ = bodyReceived_;
        }
    }
}

void HttpResponseParser::processChunkSize() {
    auto data = readableData();

    if (expectingChunkDataCrlf_) {
        if (data.empty()) {
            return;
        }
        if (data.size() < kCRLF.size() && data.front() == '\r') {
            return;
        }
        if (!data.starts_with(kCRLF)) {
            LogE("[HttpResponseParser::processChunkSize] missing chunk data CRLF");
            fail(slark::Error::HttpChunkedError);
            return;
        }
        consume(kCRLF.size());
        expectingChunkDataCrlf_ = false;
        data = readableData();
    }

    auto pos = data.find(kCRLF);
    if (pos == std::string_view::npos) {
        return;
    }

    auto sizeStr = data.substr(0, pos);

    // chunk-size may carry a chunk-extension
    auto semicolonPos = sizeStr.find(';');
    if (semicolonPos != std::string_view::npos) {
        sizeStr = sizeStr.substr(0, semicolonPos);
    }

    int64_t chunkSize = 0;
    if (!parseNonNegativeInt64(sizeStr, 16, chunkSize)) {
        LogE("[HttpResponseParser::processChunkSize] invalid chunk size: {}", std::string(sizeStr));
        fail(slark::Error::HttpChunkedError);
        return;
    }

    consume(pos + kCRLF.size());

    if (chunkSize == 0) {
        LogI("[HttpResponseParser::processChunkSize] last chunk, entering trailer, totalChunks={}",
             perf_ ? perf_->parser.chunkCount : 0);
        // Last chunk, move to trailer processing
        state_ = State::ChunkTrailer;
    } else {
        if (static_cast<uint64_t>(bodyReceived_) > maxResponseBodySize_ ||
            static_cast<uint64_t>(chunkSize) >
                maxResponseBodySize_ - static_cast<uint64_t>(bodyReceived_)) {
            LogE("[HttpResponseParser::processChunkSize] response body exceeds limit: received={} "
                 "chunk={} limit={}",
                 bodyReceived_, chunkSize, maxResponseBodySize_);
            fail(slark::Error::HttpResponseBodyTooLarge);
            return;
        }
        currentChunkSize_ = chunkSize;
        chunkBytesRead_ = 0;
        state_ = State::ChunkData;
        // Count chunks
        if (perf_) {
            perf_->parser.chunkCount++;
        }
    }
}

void HttpResponseParser::processChunkData() {
    auto data = readableData();
    if (data.empty()) {
        return;
    }

    auto remaining = currentChunkSize_ - chunkBytesRead_;
    auto available = static_cast<int64_t>(data.size());
    auto toConsume = std::min(available, remaining);

    if (toConsume > 0 && callbacks_.onBodyData) {
        callbacks_.onBodyData(reinterpret_cast<const Byte*>(data.data()),
                              static_cast<size_t>(toConsume));
    }

    bodyReceived_ += toConsume;
    chunkBytesRead_ += toConsume;
    consume(static_cast<size_t>(toConsume));

    if (chunkBytesRead_ >= currentChunkSize_) {
        expectingChunkDataCrlf_ = true;
        state_ = State::ChunkSize;
    }
}

void HttpResponseParser::processChunkTrailer() {
    auto data = readableData();

    // Fail immediately when the cumulative length exceeds the limit to prevent OOM.
    // Note: trailerBytesRead_ accumulates only on consume (C-14); data.size() is the
    // unconsumed length. Their sum approximates the current trailer region without
    // double counting across feed boundaries.
    if (trailerBytesRead_ + data.size() > kMaxTrailerSize) {
        LogE("[HttpResponseParser::processChunkTrailer] trailer overflow: trailerBytes={} chunk={}",
             trailerBytesRead_, data.size());
        fail(slark::Error::HttpResponseInvalid);
        return;
    }

    // Trailer ends with a blank line CRLF
    auto pos = data.find(kCRLF);
    if (pos == std::string_view::npos) {
        return;  // insufficient data, wait for next feed; not counted (readableData starts at
                 // readPos_, unconsumed bytes are not double counted)
    }

    if (pos == 0) {
        // Blank line, trailer ends
        consume(kCRLF.size());
        LogI("[HttpResponseParser::processChunkTrailer] complete after trailer, totalChunks={}",
             perf_ ? perf_->parser.chunkCount : 0);
        complete();
    } else {
        const auto trailerLine = data.substr(0, pos);
        const auto colonPos = trailerLine.find(':');
        if (colonPos == std::string_view::npos || colonPos == 0) {
            LogE("[HttpResponseParser::processChunkTrailer] malformed trailer");
            fail(slark::Error::HttpResponseInvalid);
            return;
        }

        // Skip the trailer header, counting only the bytes actually consumed this time
        auto consumed = pos + kCRLF.size();
        trailerBytesRead_ += consumed;
        consume(consumed);
    }
}

}  // namespace slark::http
