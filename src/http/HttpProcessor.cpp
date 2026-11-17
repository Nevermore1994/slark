//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//
#include "HttpProcessor.h"

#include "Clock.h"
#include "ConnectionFactory.h"
#include "CoroutineScheduler.hpp"
#include "Http1Session.h"
#include "HttpExecutionSupport.h"
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

Task<OpenConnectionResult> openRequestConnection(const RequestInfo& requestInfo,
                                                 const Url& url,
                                                 RequestPerf& perf) {
    if (co_await coro::cancellation_requested()) {
        co_return std::unexpected(ErrorInfo{slark::Error::Cancelled});
    }

    auto result = co_await openConnection(
        url.host, url.portNumber(), url.scheme, requestInfo.ipVersion,
        requestInfo.isRedirectTarget ? requestInfo.networkAddressPolicy
                                     : NetworkAddressPolicy::AllowAny,
        requestInfo.connectTimeout, requestInfo.tlsHandshakeTimeout, &perf);
    if (!result) {
        LogE("[HttpProcessor::openRequestConnection] failed: error={} url={}",
             toString(result.error().retCode), requestInfo.url);
        co_return std::unexpected(result.error());
    }

    auto conn = std::move(*result);
    if (!conn || !conn->isValid()) {
        co_return std::unexpected(ErrorInfo{slark::Error::ConnectionFailed});
    }
    conn->setPerf(&perf);
    co_return std::move(conn);
}

Task<ErrorInfo> sendRequest(OwnedConnection& conn, const RequestInfo& requestInfo, const Url& url) {
    const auto request = Util::buildRequestString(requestInfo, url, false);
    auto error = co_await sendRequestPart(conn, request, requestInfo, "headers");
    if (error.retCode != slark::Error::Success || requestInfo.bodyEmpty()) {
        co_return error;
    }

    const std::string_view body(reinterpret_cast<const char*>(requestInfo.body->data()),
                                requestInfo.body->size());
    co_return co_await sendRequestPart(conn, body, requestInfo, "body");
}

[[nodiscard]] bool isTerminalEvent(const HttpResponseEvent& event) noexcept {
    return event.type == HttpResponseEventType::Completed ||
           event.type == HttpResponseEventType::Error;
}

struct RedirectDecision {
    std::optional<RequestInfo> request;
    bool blocked = false;
};

RedirectDecision evaluateRedirect(const HttpResponseEvent& headers,
                                  const RequestInfo& currentInfo,
                                  int32_t redirectCount,
                                  const RedirectPolicy& policy) {
    HttpResponse response;
    response.statusCode = headers.header.httpStatusCode;
    response.headers = headers.header.headers;
    if (!policy.shouldRedirect(response, currentInfo, redirectCount)) {
        return {};
    }

    auto location = policy.extractRedirectLocation(headers.header.headers);
    if (!location.has_value()) {
        return {};
    }

    auto request = RedirectPolicy::buildRedirectRequest(
        currentInfo, headers.header.httpStatusCode, std::move(*location));
    if (!request.has_value()) {
        return {.request = std::nullopt, .blocked = true};
    }
    return {.request = std::move(request)};
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

Task<ResponseResult> HttpProcessor::process(RequestInfo requestInfo) {
    return collectResponse(std::move(requestInfo));
}

Task<ResponseResult> HttpProcessor::process(RequestInfo requestInfo, CancellationToken token) {
    auto task = process(std::move(requestInfo));
    task.attach(SpawnEdge{}, &token);
    return task;
}

Task<ResponseResult> HttpProcessor::collectResponse(RequestInfo requestInfo) {
    auto events = processEvents(std::move(requestInfo));
    HttpResponse response;
    DataPtr body = std::make_unique<Data>();
    bool hasHeaders = false;

    while (true) {
        auto event = co_await events.next();
        if (!event.has_value()) {
            co_return std::unexpected((co_await coro::cancellation_requested())
                                          ? ErrorInfo{slark::Error::Cancelled}
                                          : ErrorInfo{slark::Error::ConnectionReset});
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
                    response.body = body->empty() ? nullptr : std::move(body);
                    response.trailers = std::move(event->trailers);
                    response.error = event->error;
                    response.perf = std::move(event->perf);
                    co_return std::move(response);
                }
                co_return std::unexpected(event->error);
        }
    }
}

AsyncGenerator<HttpResponseEvent> HttpProcessor::processEvents(RequestInfo requestInfo) {
    auto currentInfo = std::move(requestInfo);
    int32_t redirectCount = 0;
    RedirectPolicy redirectPolicy;

    auto events = executeEventsSingle(currentInfo);
    std::optional<HttpResponseEvent> connectedEvent;
    std::optional<RequestInfo> pendingRedirect;

    while (true) {
        auto event = co_await events.next();
        if (!event.has_value()) {
            co_yield HttpResponseEvent::makeError(
                (co_await coro::cancellation_requested())
                    ? ErrorInfo{slark::Error::Cancelled}
                    : ErrorInfo{slark::Error::ConnectionReset});
            co_return;
        }

        // Drain the redirected response to its terminal event before starting the next request.
        if (pendingRedirect.has_value()) {
            if (isTerminalEvent(*event)) {
                currentInfo = std::move(*pendingRedirect);
                pendingRedirect.reset();
                connectedEvent.reset();
                ++redirectCount;
                events = executeEventsSingle(currentInfo);
            }
            continue;
        }

        if (event->type == HttpResponseEventType::Connected) {
            connectedEvent = std::move(*event);
            continue;
        }

        if (event->type == HttpResponseEventType::Headers) {
            auto decision = evaluateRedirect(*event, currentInfo, redirectCount, redirectPolicy);
            if (decision.blocked) {
                co_yield HttpResponseEvent::makeError(
                    ErrorInfo{slark::Error::SchemeDowngradeBlocked});
                co_return;
            }
            if (decision.request.has_value()) {
                pendingRedirect = std::move(decision.request);
                continue;
            }
        }

        const bool terminal = isTerminalEvent(*event);
        if (connectedEvent.has_value()) {
            co_yield std::move(*connectedEvent);
            connectedEvent.reset();
        }

        co_yield std::move(*event);
        if (terminal) {
            co_return;
        }
    }
}

AsyncGenerator<HttpResponseEvent> HttpProcessor::executeEventsSingle(RequestInfo requestInfo) {
    const auto requestStart = Time::nowTimeStamp();
    RequestPerf perf;
    perf.host = requestInfo.url;

    LogI("[HttpProcessor::executeEventsSingle] enter url={} method={}", requestInfo.url,
         static_cast<int>(requestInfo.methodType));

    if (co_await coro::cancellation_requested()) {
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
        // Read the domain before requestInfo is moved into the request.
        const auto requestExecutor = requestInfo.session->executor();
        auto sessionEvents = requestInfo.session->executeEvents(std::move(requestInfo));
        // Bind the producer to the session's execution domain before it is first driven.
        sessionEvents.setExecutor(requestExecutor);
        while (true) {
            auto event = co_await sessionEvents.next();
            if (!event.has_value()) {
                co_yield HttpResponseEvent::makeError(
                    (co_await coro::cancellation_requested())
                        ? ErrorInfo{slark::Error::Cancelled}
                        : ErrorInfo{slark::Error::ConnectionReset},
                    std::move(perf));
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

    auto connectionResult = co_await openRequestConnection(requestInfo, *parsed.url, perf);
    if (!connectionResult) {
        co_yield HttpResponseEvent::makeError(connectionResult.error(), std::move(perf));
        co_return;
    }
    auto conn = std::move(*connectionResult);

    const auto sendError = co_await sendRequest(*conn, requestInfo, *parsed.url);
    if (sendError.retCode != slark::Error::Success) {
        co_yield HttpResponseEvent::makeError(sendError, std::move(perf));
        co_return;
    }
    co_yield HttpResponseEvent::makeConnected();

    ParserEventState parserState;
    HttpResponseParser parser(makeParserCallbacks(parserState, perf));
    parser.setPerf(&perf);
    parser.setRequestMethod(requestInfo.methodType);
    parser.setMaxResponseBodySize(requestInfo.maxResponseBodySize);

    constexpr size_t kReadBufferSize = 16384;
    std::array<Byte, kReadBufferSize> readBuffer{};
    Time::Deadline overallDeadline(requestInfo.readTimeout);
    int64_t lastLoggedSize = 0;
    size_t recvLoops = 0;

    while (!parser.isCompleted() && !parser.hasError()) {
        auto read = co_await http::readStreamChunk(conn, readBuffer, overallDeadline, requestInfo,
                                                   perf, "HttpProcessor::readStreamChunk");
        if (read.status == StreamReadStatus::Error) {
            co_yield HttpResponseEvent::makeError(std::move(read.error), std::move(perf));
            co_return;
        }
        if (read.status == StreamReadStatus::Eof) {
            parser.onEof();
            break;
        }

        ++recvLoops;
        parser.feed(readBuffer.data(), read.bytesRead);

        if (parserState.hasHeaders) {
            co_yield HttpResponseEvent::makeHeaders(std::move(parserState.header));
            parserState.hasHeaders = false;
        }

        if (parserState.pendingBody) {
            co_yield HttpResponseEvent::makeData(std::move(parserState.pendingBody));
        }

        if (perf.parser.bodySize - lastLoggedSize >= streamProgressLogBytes()) {
            LogI("[HttpProcessor::executeEventsSingle] progress: bodySize={} recvLoops={} url={}",
                 perf.parser.bodySize, recvLoops, requestInfo.url);
            lastLoggedSize = perf.parser.bodySize;
        }

        if (parserState.parseError) {
            co_yield HttpResponseEvent::makeError(ErrorInfo{parserState.parseErrorCode},
                                                  std::move(perf));
            co_return;
        }
    }

    if (parser.hasError() || parserState.parseError || !parser.isCompleted()) {
        const auto error = parserState.parseErrorCode == slark::Error::Success
                               ? ErrorInfo{slark::Error::ConnectionReset}
                               : ErrorInfo{parserState.parseErrorCode};
        co_yield HttpResponseEvent::makeError(error, std::move(perf));
        co_return;
    }

    perf.totalRequestTimeMs =
        (Time::nowTimeStamp() - requestStart).toMilliSeconds().count();
    const bool shouldClose = parser.shouldCloseConnection();
    co_yield makeResponseTerminalEvent(parser, std::move(perf), std::move(parserState.trailers));
    if (shouldClose) {
        conn->close();
    }
}

AsyncGenerator<StreamChunk> HttpProcessor::processStreaming(RequestInfo requestInfo) {
    auto events = processEvents(std::move(requestInfo));
    while (true) {
        auto event = co_await events.next();
        if (!event.has_value()) {
            co_yield StreamChunk::makeError((co_await coro::cancellation_requested())
                                                ? ErrorInfo{slark::Error::Cancelled}
                                                : ErrorInfo{slark::Error::ConnectionReset});
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
                // The consumer may have cancelled between the header and this body chunk
                // (both come from the same read). Once cancelled, no further data is
                // delivered; the stream ends with Cancelled instead.
                if (co_await coro::cancellation_requested()) {
                    co_yield StreamChunk::makeError(ErrorInfo{slark::Error::Cancelled});
                    co_return;
                }
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

AsyncGenerator<StreamChunk> HttpProcessor::processStreaming(RequestInfo requestInfo,
                                                            CancellationToken token) {
    auto stream = processStreaming(std::move(requestInfo));
    stream.attach(SpawnEdge{}, &token);
    return stream;
}

}  // namespace slark::http
