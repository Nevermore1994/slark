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
            co_return std::unexpected(token.isCancelled() ? ErrorInfo{slark::Error::Cancelled}
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
                co_yield HttpResponseEvent::makeError(
                    token.isCancelled() ? ErrorInfo{slark::Error::Cancelled}
                                         : ErrorInfo{slark::Error::ConnectionReset});
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
                    token.isCancelled() ? ErrorInfo{slark::Error::Cancelled}
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
                                                   perf, token, "HttpProcessor::readStreamChunk");
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

AsyncGenerator<StreamChunk> HttpProcessor::processStreaming(RequestInfo requestInfo,
                                                            CancellationToken token) {
    auto events = processEvents(std::move(requestInfo), token);
    while (true) {
        auto event = co_await events.next();
        if (!event.has_value()) {
            co_yield StreamChunk::makeError(token.isCancelled()
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
