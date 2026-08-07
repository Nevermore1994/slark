//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//
#include "HttpProcessor.h"

#include "Clock.h"
#include "ConnectionPool.h"
#include "CoroutineScheduler.hpp"
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

// 建立连接并发送请求头和请求体，供统一事件源使用。
struct RequestSetup {
    std::unique_ptr<PooledConnection> conn;
    ErrorInfo error;

    [[nodiscard]] bool hasError() const noexcept {
        return error.retCode != slark::Error::Success;
    }
};

Task<RequestSetup> setupConnectionAndSend(std::string_view tag,
                                          const RequestInfo& requestInfo,
                                          const Url& url,
                                          RequestPerf& perf,
                                          CancellationToken& token) {
    if (token.isCancelled()) {
        co_return RequestSetup{nullptr, ErrorInfo{slark::Error::Cancelled}};
    }
    if (!Util::validateRequest(requestInfo, url)) {
        co_return RequestSetup{nullptr, ErrorInfo{slark::Error::InvalidArgument}};
    }
    auto conn = co_await co_acquire_connection(
        url.host, url.portNumber(), url.scheme, requestInfo.ipVersion,
        requestInfo.isRedirectTarget ? requestInfo.networkAddressPolicy
                                     : NetworkAddressPolicy::AllowAny,
        requestInfo.connectTimeout, requestInfo.tlsHandshakeTimeout, &perf, token);
    if (!conn || !conn->isValid()) {
        if (token.isCancelled()) {
            co_return RequestSetup{nullptr, ErrorInfo{slark::Error::Cancelled}};
        }
        LogE("[HttpProcessor::{}] connect failed: timeoutSource={} url={}", tag,
             static_cast<int>(perf.transport.timeoutSource), requestInfo.url);
        if (perf.connection.connectResult == slark::Error::HttpSslError ||
            perf.connection.connectResult == slark::Error::HttpSslHandshakeFailed ||
            perf.connection.connectResult == slark::Error::DnsResolveFailed ||
            perf.connection.connectResult == slark::Error::HttpRedirectError) {
            co_return RequestSetup{nullptr, ErrorInfo{perf.connection.connectResult}};
        }
        if (perf.transport.timeoutSource == TimeoutSource::Connect ||
            perf.transport.timeoutSource == TimeoutSource::TlsHandshake ||
            perf.connection.connectResult == slark::Error::ConnectionTimeout) {
            co_return RequestSetup{nullptr, ErrorInfo{slark::Error::ConnectionTimeout}};
        }
        co_return RequestSetup{nullptr, ErrorInfo{slark::Error::ConnectionFailed}};
    }
    conn->setPerf(&perf);

    if (token.isCancelled()) {
        co_return RequestSetup{std::move(conn), ErrorInfo{slark::Error::Cancelled}};
    }
    bool keepAlive = true;
    auto requestStr = Util::buildRequestString(requestInfo, url, keepAlive);
    auto sendResult = co_await conn->asyncSend(requestStr, requestInfo.writeTimeout, token);
    if (!sendResult) {
        if (token.isCancelled()) {
            co_return RequestSetup{std::move(conn), ErrorInfo{slark::Error::Cancelled}};
        }
        LogE("[HttpProcessor::{}] send failed: legacyErr={} url={}", tag,
             static_cast<int>(sendResult.error()), requestInfo.url);
        co_return RequestSetup{std::move(conn),
                               ErrorInfo{mapLegacyErrorToUnified(sendResult.error())}};
    }

    if (requestInfo.bodyEmpty()) {
        co_return RequestSetup{std::move(conn), ErrorInfo{}};
    }
    if (token.isCancelled()) {
        co_return RequestSetup{std::move(conn), ErrorInfo{slark::Error::Cancelled}};
    }
    std::string_view bodyView(reinterpret_cast<const char*>(requestInfo.body->data()),
                              requestInfo.body->size());
    auto bodyResult = co_await conn->asyncSend(bodyView, requestInfo.writeTimeout, token);
    if (!bodyResult) {
        if (token.isCancelled()) {
            co_return RequestSetup{std::move(conn), ErrorInfo{slark::Error::Cancelled}};
        }
        LogE("[HttpProcessor::{}] sendBody failed: legacyErr={} url={}", tag,
             static_cast<int>(bodyResult.error()), requestInfo.url);
        co_return RequestSetup{std::move(conn),
                               ErrorInfo{mapLegacyErrorToUnified(bodyResult.error())}};
    }
    co_return RequestSetup{std::move(conn), ErrorInfo{}};
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

Task<StreamReadResult> readStreamChunk(std::unique_ptr<PooledConnection>& conn,
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

            if (event->type == HttpResponseEventType::Headers) {
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
                        break;
                    }
                }
            }

            if (connectedEvent.has_value()) {
                co_yield std::move(*connectedEvent);
                connectedEvent.reset();
            }

            const bool terminal = event->type == HttpResponseEventType::Completed ||
                                  event->type == HttpResponseEventType::Error;
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

    auto setup = co_await setupConnectionAndSend("executeEventsSingle", requestInfo, *parsed.url,
                                                 perf, token);
    if (setup.hasError()) {
        co_yield HttpResponseEvent::makeError(setup.error, std::move(perf));
        co_return;
    }
    auto conn = std::move(setup.conn);
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
