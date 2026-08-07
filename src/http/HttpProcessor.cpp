//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//
#include "HttpProcessor.h"

#include "Clock.h"
#include "ConnectionFactory.h"
#include "CoroutineScheduler.hpp"
#include "Http1Session.h"
#include "HttpResponseParser.h"
#include "HttpUtil.hpp"
#include "Log.hpp"
#include "LogConfig.h"
#include "Url.h"

namespace slark::http {

namespace {

inline int64_t streamProgressLogBytes() {
    return static_cast<int64_t>(getLogConfig().progressLogBytes);
}

Error mapLegacyErrorToUnified(Error error) noexcept {
    switch (error) {
        case Error::ConnectionTimeout:
            return slark::Error::ConnectionTimeout;
        case Error::ConnectionReset:
        case Error::ConnectionRefused:
        case Error::BrokenPipe:
        case Error::SocketClosed:
        case Error::SocketError:
        case Error::ConnectionFailed:
            return slark::Error::ConnectionReset;
        default:
            return slark::Error::Failed;
    }
}

Task<OpenConnectionResult> openRequestConnection(const RequestInfo& requestInfo,
                                          const Url& url,
                                          RequestPerf& perf,
                                          CancellationToken& token) {
    if (token.isCancelled()) {
        co_return std::unexpected(ErrorInfo{slark::Error::Cancelled});
    }

    auto result = co_await openConnection(
        url.host, url.portNumber(), url.scheme, requestInfo.ipVersion,
        requestInfo.isRedirectTarget ? requestInfo.networkAddressPolicy
                                     : NetworkAddressPolicy::AllowAny,
        requestInfo.connectTimeout, requestInfo.tlsHandshakeTimeout, &perf, token);
    if (!result) {
        LogE("[HttpProcessor::openRequestConnection] failed: error={} url={}",
             static_cast<int>(result.error().retCode), requestInfo.url);
        co_return std::unexpected(result.error());
    }

    auto conn = std::move(*result);
    if (!conn || !conn->isValid()) {
        co_return std::unexpected(ErrorInfo{slark::Error::ConnectionFailed});
    }
    conn->setPerf(&perf);
    co_return std::move(conn);
}

Task<ErrorInfo> sendRequestPart(OwnedConnection& conn,
                                std::string_view data,
                                const RequestInfo& requestInfo,
                                CancellationToken& token,
                                std::string_view partName) {
    if (token.isCancelled()) {
        co_return ErrorInfo{slark::Error::Cancelled};
    }

    auto result = co_await conn.asyncSend(data, requestInfo.writeTimeout, token);
    if (result) {
        co_return ErrorInfo{};
    }
    if (token.isCancelled()) {
        co_return ErrorInfo{slark::Error::Cancelled};
    }

    const auto error = mapLegacyErrorToUnified(result.error());
    LogE("[HttpProcessor::sendRequestPart] {} failed: error={} legacyErr={} url={}", partName,
         static_cast<int>(error), static_cast<int>(result.error()), requestInfo.url);
    co_return ErrorInfo{error};
}

Task<ErrorInfo> sendRequest(OwnedConnection& conn,
                            const RequestInfo& requestInfo,
                            const Url& url,
                            CancellationToken& token) {
    const auto request = Util::buildRequestString(requestInfo, url, false);
    auto error = co_await sendRequestPart(conn, request, requestInfo, token, "headers");
    if (error.retCode != slark::Error::Success || requestInfo.bodyEmpty()) {
        co_return error;
    }

    const std::string_view body(reinterpret_cast<const char*>(requestInfo.body->data()),
                                requestInfo.body->size());
    co_return co_await sendRequestPart(conn, body, requestInfo, token, "body");
}

HttpResponseParser::Callbacks makeEventCallbacks(
    ResponseHeader& header,
    bool& hasHeaders,
    std::unordered_map<std::string, std::string>& trailers,
    DataPtr& pendingBody,
    RequestPerf& perf,
    bool& parseError,
    Error& parseErrorCode) {
    HttpResponseParser::Callbacks callbacks;
    callbacks.onStatusLine = [&header](HttpStatusCode code, const std::string& reason) {
        header.httpStatusCode = code;
        header.reasonPhrase = reason;
    };
    callbacks.onHeaders = [&header, &hasHeaders](
                              std::unordered_map<std::string, std::string>&& headers) {
        header.headers = std::move(headers);
        hasHeaders = true;
    };
    callbacks.onTrailers = [&trailers](std::unordered_map<std::string, std::string>&& values) {
        trailers = std::move(values);
    };
    callbacks.onBodyData = [&pendingBody, &perf](const Byte* data, size_t size) {
        if (!pendingBody) {
            pendingBody = std::make_unique<Data>();
        }
        pendingBody->append(ConstByteSpan(data, size));
        perf.parser.bodySize += static_cast<int64_t>(size);
    };
    callbacks.onError = [&parseError, &parseErrorCode, &perf](Error code) {
        parseError = true;
        parseErrorCode = code;
        perf.parser.parseResult = code;
    };
    return callbacks;
}

std::optional<ErrorInfo> classifyStreamReadError(Error error,
                                                 bool overallExpired,
                                                 RequestPerf& perf) noexcept {
    if (error != Error::ConnectionTimeout) {
        return std::nullopt;
    }
    perf.transport.timeoutSource =
        overallExpired ? TimeoutSource::ReadOverall : TimeoutSource::ReadIdle;
    return ErrorInfo{slark::Error::ConnectionTimeout};
}

struct StreamReadResult {
    bool shouldExit = false;
    bool isEof = false;
    size_t bytesRead = 0;
    ErrorInfo error;
};

Task<StreamReadResult> readStreamChunk(std::unique_ptr<OwnedConnection>& conn,
                                       std::array<Byte, 16384>& readBuffer,
                                       const Time::Deadline& overallDeadline,
                                       const RequestInfo& requestInfo,
                                       RequestPerf& perf,
                                       CancellationToken& token) {
    StreamReadResult result;

    if (token.isCancelled()) {
        result.shouldExit = true;
        result.error = ErrorInfo{slark::Error::Cancelled};
        co_return result;
    }

    if (overallDeadline.isExpired()) {
        perf.transport.timeoutSource = TimeoutSource::ReadOverall;
        LogE("[HttpProcessor::readStreamChunk] overall deadline expired: bodySize={} url={}",
             perf.parser.bodySize, requestInfo.url);
        result.shouldExit = true;
        result.error = ErrorInfo{slark::Error::ConnectionTimeout};
        co_return result;
    }

    auto socket = conn->get();
    if (!socket || !socket->isValid()) {
        LogE("[HttpProcessor::readStreamChunk] socket invalid: bodySize={} url={}",
             perf.parser.bodySize, requestInfo.url);
        result.shouldExit = true;
        result.error = ErrorInfo{slark::Error::ConnectionReset};
        co_return result;
    }

    const auto readTimeout = std::min(requestInfo.readIdleTimeout, overallDeadline.remaining());
    auto readResult =
        co_await socket->asyncReceive(readBuffer.data(), readBuffer.size(), readTimeout, token);
    if (!readResult.has_value()) {
        if (token.isCancelled()) {
            result.shouldExit = true;
            result.error = ErrorInfo{slark::Error::Cancelled};
            co_return result;
        }
        auto errorInfo =
            classifyStreamReadError(readResult.error(), overallDeadline.isExpired(), perf);
        if (errorInfo.has_value()) {
            LogE("[HttpProcessor::readStreamChunk] read timeout: timeoutSource={} bodySize={} "
                 "url={}",
                 static_cast<int>(perf.transport.timeoutSource), perf.parser.bodySize,
                 requestInfo.url);
            result.shouldExit = true;
            result.error = std::move(*errorInfo);
            co_return result;
        }
        LogE("[HttpProcessor::readStreamChunk] read error: err={} bodySize={} url={}",
             static_cast<int>(readResult.error()), perf.parser.bodySize, requestInfo.url);
        result.shouldExit = true;
        result.error = ErrorInfo{slark::Error::ConnectionReset};
        co_return result;
    }

    if (*readResult == 0) {
        result.isEof = true;
        co_return result;
    }

    result.bytesRead = *readResult;
    co_return result;
}

}  // namespace

HttpProcessor::ParsedRequest HttpProcessor::parseUrl(const std::string& urlStr) {
    ParsedRequest result;
    auto url = Url(urlStr);
    if (!url.isValid()) {
        result.error = ErrorInfo{slark::Error::InvalidArgument, static_cast<int32_t>(url.error())};
        return result;
    }
    if (!url.isSupportScheme()) {
        result.error = ErrorInfo{slark::Error::SchemeNotSupported};
        return result;
    }
    result.url = std::move(url);
    result.isValid = true;
    return result;
}

Task<ResponseResult> HttpProcessor::process(RequestInfo requestInfo, CancellationToken token) {
    return aggregateEvents(std::move(requestInfo), std::move(token));
}

Task<ResponseResult> HttpProcessor::aggregateEvents(RequestInfo requestInfo,
                                                     CancellationToken token) {
    auto events = processEvents(std::move(requestInfo), token);
    HttpResponse response;
    DataPtr body = std::make_unique<Data>();
    bool hasHeaders = false;

    while (true) {
        auto event = co_await events.next();
        if (!event.has_value()) {
            co_return std::unexpected(ErrorInfo{slark::Error::ConnectionReset});
        }

        switch (event->type) {
            case HttpResponseEventType::Connected:
                break;
            case HttpResponseEventType::Headers:
                response.statusCode = event->header.httpStatusCode;
                response.reasonPhrase = event->header.reasonPhrase;
                response.headers = std::move(event->header.headers);
                hasHeaders = true;
                break;
            case HttpResponseEventType::Data:
                if (event->data) {
                    body->append(ConstByteSpan(event->data->data(), event->data->length()));
                }
                break;
            case HttpResponseEventType::Completed:
                response.trailers = std::move(event->trailers);
                response.body = body->empty() ? nullptr : std::move(body);
                response.error = (response.statusCode >= HttpStatusCode::BadRequest)
                                     ? ErrorInfo{slark::Error::HttpError,
                                                 static_cast<int32_t>(response.statusCode)}
                                     : ErrorInfo{slark::Error::Success};
                response.perf = std::move(event->perf);
                co_return std::move(response);
            case HttpResponseEventType::Error:
                if (event->error.retCode == slark::Error::HttpError && hasHeaders) {
                    response.error = event->error;
                    response.perf = std::move(event->perf);
                    co_return std::move(response);
                }
                co_return std::unexpected(event->error);
        }
    }
}

AsyncGenerator<HttpResponseEvent> HttpProcessor::processEvents(RequestInfo requestInfo,
                                                                CancellationToken token) {
    auto currentInfo = std::move(requestInfo);
    int32_t redirectCount = 0;
    RedirectPolicy redirectPolicy;

    while (true) {
        auto events = executeEventsSingle(currentInfo, token);
        std::optional<HttpResponseEvent> connectedEvent;
        bool redirected = false;

        while (true) {
            auto event = co_await events.next();
            if (!event.has_value()) {
                co_yield HttpResponseEvent::makeError(ErrorInfo{slark::Error::ConnectionReset});
                co_return;
            }

            if (event->type == HttpResponseEventType::Connected) {
                connectedEvent = std::move(*event);
                continue;
            }

            if (event->type == HttpResponseEventType::Headers && !redirected) {
                HttpResponse headerResponse;
                headerResponse.statusCode = event->header.httpStatusCode;
                headerResponse.headers = event->header.headers;
                if (redirectPolicy.shouldRedirect(headerResponse, currentInfo, redirectCount)) {
                    auto location = redirectPolicy.extractRedirectLocation(event->header.headers);
                    if (location.has_value()) {
                        auto redirectRequest = RedirectPolicy::buildRedirectRequest(
                            currentInfo, event->header.httpStatusCode, std::move(*location));
                        if (!redirectRequest.has_value()) {
                            co_yield HttpResponseEvent::makeError(
                                ErrorInfo{slark::Error::SchemeDowngradeBlocked});
                            co_return;
                        }
                        currentInfo = std::move(*redirectRequest);
                        ++redirectCount;
                        redirected = true;
                    }
                }
            }

            const bool terminal = event->type == HttpResponseEventType::Completed ||
                                  event->type == HttpResponseEventType::Error;
            if (redirected) {
                if (terminal) {
                    break;
                }
                continue;
            }

            if (connectedEvent.has_value()) {
                co_yield std::move(*connectedEvent);
                connectedEvent.reset();
            }

            co_yield std::move(*event);
            if (terminal) {
                co_return;
            }
        }

        if (!redirected) {
            co_return;
        }
    }
}

AsyncGenerator<HttpResponseEvent> HttpProcessor::executeEventsSingle(
    RequestInfo requestInfo,
    CancellationToken token) {
    const auto requestStart = Time::nowTimeStamp();
    RequestPerf perf;
    perf.host = requestInfo.url;

    LogI("[HttpProcessor::executeEventsSingle] enter url={} method={}", requestInfo.url,
         static_cast<int>(requestInfo.methodType));

    if (token.isCancelled()) {
        co_yield HttpResponseEvent::makeError(ErrorInfo{slark::Error::Cancelled}, std::move(perf));
        co_return;
    }

    auto parsed = parseUrl(requestInfo.url);
    if (!parsed.isValid) {
        co_yield HttpResponseEvent::makeError(parsed.error, std::move(perf));
        co_return;
    }

    if (!Util::validateRequest(requestInfo, *parsed.url)) {
        co_yield HttpResponseEvent::makeError(
            ErrorInfo{slark::Error::InvalidArgument}, std::move(perf));
        co_return;
    }

    if (requestInfo.connectionMode == HttpConnectionMode::KeepAlive) {
        if (!requestInfo.session) {
            co_yield HttpResponseEvent::makeError(
                ErrorInfo{slark::Error::InvalidOperation}, std::move(perf));
            co_return;
        }
        auto sessionEvents = requestInfo.session->executeEvents(std::move(requestInfo), token);
        while (true) {
            auto event = co_await sessionEvents.next();
            if (!event.has_value()) {
                co_yield HttpResponseEvent::makeError(
                    ErrorInfo{slark::Error::ConnectionReset}, std::move(perf));
                co_return;
            }
            const bool terminal = event->type == HttpResponseEventType::Completed ||
                                  event->type == HttpResponseEventType::Error;
            co_yield std::move(*event);
            if (terminal) {
                co_return;
            }
        }
    }

    auto connectionResult = co_await openRequestConnection(requestInfo, *parsed.url, perf, token);
    if (!connectionResult) {
        co_yield HttpResponseEvent::makeError(connectionResult.error(), std::move(perf));
        co_return;
    }
    auto conn = std::move(*connectionResult);

    const auto sendError = co_await sendRequest(*conn, requestInfo, *parsed.url, token);
    if (sendError.retCode != slark::Error::Success) {
        co_yield HttpResponseEvent::makeError(sendError, std::move(perf));
        co_return;
    }
    co_yield HttpResponseEvent::makeConnected();

    ResponseHeader header;
    bool hasHeaders = false;
    DataPtr pendingBody;
    std::unordered_map<std::string, std::string> trailers;
    bool parseError = false;
    Error parseErrorCode = slark::Error::Success;
    HttpResponseParser parser(makeEventCallbacks(header, hasHeaders, trailers, pendingBody, perf,
                                                 parseError, parseErrorCode));
    parser.setPerf(&perf);
    parser.setRequestMethod(requestInfo.methodType);
    parser.setMaxResponseBodySize(requestInfo.maxResponseBodySize);

    constexpr size_t kReadBufferSize = 16384;
    std::array<Byte, kReadBufferSize> readBuffer{};
    Time::Deadline overallDeadline(requestInfo.readTimeout);
    int64_t lastLoggedSize = 0;
    size_t recvLoops = 0;

    while (!parser.isCompleted() && !parser.hasError()) {
        auto read = co_await readStreamChunk(conn, readBuffer, overallDeadline, requestInfo, perf,
                                             token);
        if (read.shouldExit) {
            co_yield HttpResponseEvent::makeError(std::move(read.error), std::move(perf));
            co_return;
        }
        if (read.isEof) {
            parser.onEof();
            break;
        }

        ++recvLoops;
        parser.feed(readBuffer.data(), read.bytesRead);

        if (hasHeaders) {
            const auto statusCode = header.httpStatusCode;
            co_yield HttpResponseEvent::makeHeaders(std::move(header));
            hasHeaders = false;
            if (parser.isHttpError()) {
                co_yield HttpResponseEvent::makeError(
                    ErrorInfo{slark::Error::HttpError, static_cast<int32_t>(statusCode)},
                    std::move(perf));
                co_return;
            }
        }

        if (pendingBody) {
            co_yield HttpResponseEvent::makeData(std::move(pendingBody));
        }

        if (perf.parser.bodySize - lastLoggedSize >= streamProgressLogBytes()) {
            LogI("[HttpProcessor::executeEventsSingle] progress: bodySize={} recvLoops={} url={}",
                 perf.parser.bodySize, recvLoops, requestInfo.url);
            lastLoggedSize = perf.parser.bodySize;
        }

        if (parseError) {
            co_yield HttpResponseEvent::makeError(ErrorInfo{parseErrorCode}, std::move(perf));
            co_return;
        }
    }

    if (parser.hasError() || parseError || !parser.isCompleted()) {
        const auto error = parseErrorCode == slark::Error::Success
                               ? ErrorInfo{slark::Error::ConnectionReset}
                               : ErrorInfo{parseErrorCode};
        co_yield HttpResponseEvent::makeError(error, std::move(perf));
        co_return;
    }

    perf.totalRequestTimeMs =
        (Time::nowTimeStamp() - requestStart).toMilliSeconds().count();
    const bool shouldClose = parser.shouldCloseConnection();
    co_yield HttpResponseEvent::makeCompleted(std::move(perf), std::move(trailers));
    if (shouldClose) {
        conn->close();
    }
}

AsyncGenerator<StreamChunk> HttpProcessor::processStreaming(RequestInfo requestInfo,
                                                            CancellationToken token) {
    auto events = processEvents(std::move(requestInfo), std::move(token));
    while (true) {
        auto event = co_await events.next();
        if (!event.has_value()) {
            co_return;
        }

        switch (event->type) {
            case HttpResponseEventType::Connected:
                break;
            case HttpResponseEventType::Headers:
                co_yield StreamChunk::makeHeader(event->header.httpStatusCode,
                                                 std::move(event->header.headers));
                break;
            case HttpResponseEventType::Data:
                co_yield StreamChunk::makeData(std::move(event->data));
                break;
            case HttpResponseEventType::Completed:
                co_yield StreamChunk::makeFinal(std::move(event->trailers));
                co_return;
            case HttpResponseEventType::Error:
                co_yield StreamChunk::makeError(std::move(event->error));
                co_return;
        }
    }
}

}  // namespace slark::http
