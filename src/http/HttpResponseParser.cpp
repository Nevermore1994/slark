//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//
#include "HttpResponseParser.h"

#include "DataView.hpp"
#include "Time.hpp"

namespace slark::http {

void HttpResponseParser::feed(const Byte* data, size_t size) {
    if (state_ == State::Completed || state_ == State::Error) {
        return;
    }

    auto parseStart = Time::nowTimeStamp();

    // 防止恶意服务端发送超大 header 耗尽内存
    constexpr size_t kMaxHeaderBufferSize = 64 * 1024;
    if (state_ == State::StatusLine || state_ == State::Headers) {
        if (buffer_.size() + size > kMaxHeaderBufferSize) {
            fail(ResultCode::Failed);
            return;
        }
    }

    buffer_.append(reinterpret_cast<const char*>(data), size);

    // 循环驱动状态机，直到数据不足或终态
    while (state_ != State::Completed && state_ != State::Error) {
        auto prevSize = readableSize();

        switch (state_) {
            case State::StatusLine:
                processStatusLine();
                break;
            case State::Headers:
                processHeaders();
                break;
            case State::BodyFixed:
                processBodyFixed();
                break;
            case State::BodyChunked:
            case State::ChunkSize:
                processChunkSize();
                break;
            case State::ChunkData:
                processChunkData();
                break;
            case State::ChunkTrailer:
                processChunkTrailer();
                break;
            default:
                return;
        }

        // 如果本轮没有消费任何数据，说明数据不足，等待更多数据
        if (readableSize() == prevSize && state_ != State::Completed && state_ != State::Error) {
            break;
        }
    }

    if (perf_) {
        auto parseEnd = Time::nowTimeStamp();
        // 直接使用微秒精度
        perf_->parser.headerParseTimeUs += (parseEnd - parseStart).point();
    }
}

void HttpResponseParser::reset() noexcept {
    state_ = State::StatusLine;
    buffer_.clear();
    readPos_ = 0;
    headers_.clear();
    statusCode_ = HttpStatusCode::Unknown;
    shouldClose_ = false;
    contentLength_ = -1;
    bodyReceived_ = 0;
    currentChunkSize_ = 0;
    chunkBytesRead_ = 0;
    trailerBytesRead_ = 0;
}

void HttpResponseParser::processStatusLine() {
    auto data = readableData();
    auto pos = data.find(kCRLF);
    if (pos == std::string_view::npos) {
        return;
    }

    auto line = data.substr(0, pos);

    // "HTTP/1.1 200 OK"
    constexpr std::string_view kHTTPFlag = "HTTP/";
    if (line.find(kHTTPFlag) == std::string_view::npos) {
        fail(ResultCode::Failed);
        return;
    }

    auto firstSpace = line.find(' ');
    if (firstSpace == std::string_view::npos) {
        fail(ResultCode::Failed);
        return;
    }

    auto afterVersion = line.substr(firstSpace + 1);
    auto secondSpace = afterVersion.find(' ');
    if (secondSpace == std::string_view::npos) {
        fail(ResultCode::Failed);
        return;
    }

    auto statusCodeStr = afterVersion.substr(0, secondSpace);
    auto reasonPhrase = afterVersion.substr(secondSpace + 1);

    auto codeResult = StringUtil::fromChars<int32_t>(statusCodeStr);
    if (!codeResult) {
        fail(ResultCode::Failed);
        return;
    }

    statusCode_ = static_cast<HttpStatusCode>(*codeResult);
    consume(pos + kCRLF.size());
    state_ = State::Headers;

    if (callbacks_.onStatusLine) {
        callbacks_.onStatusLine(statusCode_, std::string(reasonPhrase));
    }
}

void HttpResponseParser::processHeaders() {
    auto data = readableData();
    auto pos = data.find(kHeaderEnd);
    if (pos == std::string_view::npos) {
        return;
    }

    auto headerBlock = data.substr(0, pos);

    // 逐行解析 header
    size_t lineStart = 0;
    while (lineStart < headerBlock.size()) {
        auto lineEnd = headerBlock.find(kCRLF, lineStart);
        if (lineEnd == std::string_view::npos) {
            lineEnd = headerBlock.size();
        }

        auto line = headerBlock.substr(lineStart, lineEnd - lineStart);
        auto colonPos = line.find(':');
        if (colonPos != std::string_view::npos) {
            auto name = line.substr(0, colonPos);
            auto value = line.substr(colonPos + 1);
            // trim leading spaces
            while (!value.empty() && value.front() == ' ') {
                value.remove_prefix(1);
            }
            headers_[std::string(name)] = std::string(value);
        }

        lineStart = lineEnd + kCRLF.size();
    }

    consume(pos + kHeaderEnd.size());

    // 判断 Connection: close
    auto connValue = findHeader("Connection");
    shouldClose_ = (!connValue.empty() && StringUtil::iequals(connValue, "close"));

    // 判断 body 传输方式
    auto transferEncoding = findHeader("Transfer-Encoding");
    if (!transferEncoding.empty() &&
        StringUtil::toLower(transferEncoding).find("chunked") != std::string::npos) {
        state_ = State::ChunkSize;
    } else {
        auto contentLengthStr = findHeader("Content-Length");
        if (!contentLengthStr.empty()) {
            auto clResult = StringUtil::fromChars<int64_t>(contentLengthStr);
            if (!clResult || *clResult < 0) {
                fail(ResultCode::Failed);
                return;
            }
            contentLength_ = *clResult;
        }

        // RFC 7230: 特定状态码和请求方法无 body
        if (shouldSkipBody()) {
            complete();
        } else if (contentLength_ == 0) {
            // 无 body
            complete();
        } else {
            state_ = State::BodyFixed;
        }
    }

    if (callbacks_.onHeaders) {
        callbacks_.onHeaders(std::unordered_map<std::string, std::string>(headers_));
    }

    // 如果已经有 body 数据在 buffer 中，继续处理
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

        if (toConsume > 0 && callbacks_.onBodyData) {
            callbacks_.onBodyData(reinterpret_cast<const Byte*>(data.data()),
                                  static_cast<size_t>(toConsume));
        }

        bodyReceived_ += toConsume;
        consume(static_cast<size_t>(toConsume));

        if (bodyReceived_ >= contentLength_) {
            complete();
        }
    } else {
        // Content-Length 未知，消费所有可用数据
        if (callbacks_.onBodyData) {
            callbacks_.onBodyData(reinterpret_cast<const Byte*>(data.data()), data.size());
        }
        bodyReceived_ += available;
        consume(data.size());
    }
}

void HttpResponseParser::processChunkSize() {
    auto data = readableData();

    // 只跳一个 chunk 间的 CRLF（RFC 7230 §4.1 严格限定）
    if (data.starts_with(kCRLF)) {
        consume(kCRLF.size());
        data = readableData();
    }

    auto pos = data.find(kCRLF);
    if (pos == std::string_view::npos) {
        return;
    }

    auto sizeStr = data.substr(0, pos);

    // chunk-size 可能带有 chunk-extension
    auto semicolonPos = sizeStr.find(';');
    if (semicolonPos != std::string_view::npos) {
        sizeStr = sizeStr.substr(0, semicolonPos);
    }

    auto chunkSizeResult = StringUtil::fromChars<int64_t>(sizeStr, 16);
    if (!chunkSizeResult) {
        fail(ResultCode::ChunkSizeError);
        return;
    }
    auto chunkSize = *chunkSizeResult;

    consume(pos + kCRLF.size());

    if (chunkSize == 0) {
        // 最后一个 chunk，进入 trailer 处理
        state_ = State::ChunkTrailer;
    } else {
        currentChunkSize_ = chunkSize;
        chunkBytesRead_ = 0;
        state_ = State::ChunkData;
        // 统计chunk数量
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

    chunkBytesRead_ += toConsume;
    consume(static_cast<size_t>(toConsume));

    if (chunkBytesRead_ >= currentChunkSize_) {
        state_ = State::ChunkSize;
    }
}

void HttpResponseParser::processChunkTrailer() {
    auto data = readableData();

    // 累计长度超限时直接 fail，防止 OOM
    if (trailerBytesRead_ + data.size() > kMaxTrailerSize) {
        fail(ResultCode::Failed);
        return;
    }

    // trailer 以空行 CRLF 结束
    auto pos = data.find(kCRLF);
    if (pos == std::string_view::npos) {
        trailerBytesRead_ += data.size();
        return;
    }

    if (pos == 0) {
        // 空行，trailer 结束
        consume(kCRLF.size());
        complete();
    } else {
        // 跳过 trailer header
        auto consumed = pos + kCRLF.size();
        trailerBytesRead_ += consumed;
        consume(consumed);
    }
}

std::string HttpResponseParser::findHeader(std::string_view key) const {
    for (const auto& [k, v] : headers_) {
        if (StringUtil::iequals(k, key)) {
            return v;
        }
    }
    return {};
}

} // namespace slark::http
