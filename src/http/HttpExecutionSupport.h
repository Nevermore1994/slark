//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <memory>
#include <string_view>
#include <unordered_map>
#include <utility>

#include "ConnectionFactory.h"
#include "HttpResponseParser.h"
#include "Log.hpp"
#include "Task.hpp"
#include "Time.hpp"
#include "Type.h"

namespace slark::http {

struct ParserEventState final {
    ResponseHeader header;
    bool hasHeaders = false;
    DataPtr pendingBody;
    bool parseError = false;
    Error parseErrorCode = slark::Error::Success;
};

inline HttpResponseParser::Callbacks makeParserCallbacks(ParserEventState& state,
                                                         RequestPerf& perf) {
    HttpResponseParser::Callbacks callbacks;
    callbacks.onStatusLine = [&state](HttpStatusCode code, const std::string& reason) {
        state.header.httpStatusCode = code;
        state.header.reasonPhrase = reason;
    };
    callbacks.onHeaders = [&state](const std::unordered_map<std::string, std::string>&) {
        state.hasHeaders = true;
    };
    callbacks.onBodyData = [&state, &perf](const Byte* data, size_t size) {
        if (!state.pendingBody) {
            state.pendingBody = std::make_unique<Data>();
        }
        state.pendingBody->append(ConstByteSpan(data, size));
        perf.parser.bodySize += static_cast<int64_t>(size);
    };
    callbacks.onError = [&state, &perf](Error code) {
        state.parseError = true;
        state.parseErrorCode = code;
        perf.parser.parseResult = code;
    };
    return callbacks;
}

inline Task<ErrorInfo> sendRequestPart(OwnedConnection& connection,
                                       std::string_view data,
                                       const RequestInfo& requestInfo,
                                       std::string_view partName) {
    if (co_await slark::coro::cancellation_requested()) {
        co_return ErrorInfo{slark::Error::Cancelled};
    }

    auto result = co_await connection.asyncSend(data, requestInfo.writeTimeout);
    if (result) {
        co_return ErrorInfo{};
    }
    if (co_await slark::coro::cancellation_requested()) {
        co_return ErrorInfo{slark::Error::Cancelled};
    }

    const auto error = normalizeTransportError(result.error());
    LogE("[Http::sendRequestPart] {} failed: error={} legacyErr={} url={}", partName,
         toString(error), toString(result.error()), requestInfo.url);
    co_return ErrorInfo{error};
}

inline Task<ErrorInfo> sendRequestParts(OwnedConnection& connection,
                                         std::string_view request,
                                         const RequestInfo& requestInfo) {
    auto error = co_await sendRequestPart(connection, request, requestInfo, "headers");
    if (error.retCode != slark::Error::Success || requestInfo.bodyEmpty()) {
        co_return error;
    }
    const std::string_view body(reinterpret_cast<const char*>(requestInfo.body->data()),
                                requestInfo.body->size());
    co_return co_await sendRequestPart(connection, body, requestInfo, "body");
}

enum class StreamReadStatus : uint8_t {
    Data,
    Eof,
    Error,
};

struct StreamReadResult final {
    StreamReadStatus status = StreamReadStatus::Data;
    size_t bytesRead = 0;
    ErrorInfo error;
};

[[nodiscard]] inline ErrorInfo streamEndError(bool cancelled) noexcept {
    return ErrorInfo{cancelled ? slark::Error::Cancelled : slark::Error::ConnectionReset};
}

inline ErrorInfo classifyStreamReadError(Error error,
                                         bool overallExpired,
                                         RequestPerf& perf) noexcept {
    const auto unifiedError = normalizeTransportError(error);
    if (unifiedError == Error::ConnectionTimeout) {
        perf.transport.timeoutSource =
            overallExpired ? TimeoutSource::ReadOverall : TimeoutSource::ReadIdle;
    }
    return ErrorInfo{unifiedError};
}

template <typename ConnectionHolder>
Task<StreamReadResult> readStreamChunk(const ConnectionHolder& connection,
                                       std::array<Byte, 16384>& readBuffer,
                                       const Time::Deadline& overallDeadline,
                                       const RequestInfo& requestInfo,
                                       RequestPerf& perf,
                                       std::string_view logTag) {
    StreamReadResult result;

    if (co_await coro::cancellation_requested()) {
        result.status = StreamReadStatus::Error;
        result.error = ErrorInfo{slark::Error::Cancelled};
        co_return result;
    }

    if (overallDeadline.isExpired()) {
        perf.transport.timeoutSource = TimeoutSource::ReadOverall;
        LogE("[{}] overall deadline expired: bodySize={} url={}", logTag, perf.parser.bodySize,
             requestInfo.url);
        result.status = StreamReadStatus::Error;
        result.error = ErrorInfo{slark::Error::ConnectionTimeout};
        co_return result;
    }

    auto socket = connection->get();
    if (!socket || !socket->isValid()) {
        LogE("[{}] socket invalid: bodySize={} url={}", logTag, perf.parser.bodySize,
             requestInfo.url);
        result.status = StreamReadStatus::Error;
        result.error = ErrorInfo{slark::Error::ConnectionReset};
        co_return result;
    }

    const auto readTimeout = std::min(requestInfo.readIdleTimeout, overallDeadline.remaining());
    auto readResult = co_await socket->asyncReceive(readBuffer.data(), readBuffer.size(), readTimeout);
    if (!readResult) {
        if (co_await coro::cancellation_requested()) {
            result.status = StreamReadStatus::Error;
            result.error = ErrorInfo{slark::Error::Cancelled};
            co_return result;
        }

        result.status = StreamReadStatus::Error;
        result.error =
            classifyStreamReadError(readResult.error(), overallDeadline.isExpired(), perf);
        if (result.error.retCode == slark::Error::ConnectionTimeout) {
            LogE("[{}] read timeout: timeoutSource={} bodySize={} url={}", logTag,
                 static_cast<int>(perf.transport.timeoutSource), perf.parser.bodySize,
                 requestInfo.url);
        } else {
            LogE("[{}] read error: err={} bodySize={} url={}", logTag,
                 toString(result.error.retCode), perf.parser.bodySize, requestInfo.url);
        }
        co_return result;
    }

    if (*readResult == 0) {
        result.status = StreamReadStatus::Eof;
        co_return result;
    }

    result.status = StreamReadStatus::Data;
    result.bytesRead = *readResult;
    co_return result;
}

inline HttpResponseEvent makeResponseTerminalEvent(
    const HttpResponseParser& parser,
    RequestPerf perf,
    std::unordered_map<std::string, std::string> trailers = {}) {
    if (parser.isHttpError()) {
        return HttpResponseEvent::makeError(
            ErrorInfo{slark::Error::HttpError, static_cast<int32_t>(parser.statusCode())},
            std::move(perf), std::move(trailers));
    }
    return HttpResponseEvent::makeCompleted(std::move(perf), std::move(trailers));
}

}  // namespace slark::http
