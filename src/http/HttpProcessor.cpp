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

// Establish a connection and send the request (headers + body), shared by
// executeSingle/executeStreamingSingle
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
    auto conn = co_await co_acquire_connection(url.host, url.portNumber(), url.scheme,
                                               requestInfo.ipVersion, requestInfo.connectTimeout,
                                               requestInfo.tlsHandshakeTimeout, &perf, token);
    if (!conn || !conn->isValid()) {
        if (token.isCancelled()) {
            co_return RequestSetup{nullptr, ErrorInfo{slark::Error::Cancelled}};
        }
        LogE("[HttpProcessor::{}] connect failed: timeoutSource={} url={}", tag,
             static_cast<int>(perf.transport.timeoutSource), requestInfo.url);
        if (perf.transport.timeoutSource == TimeoutSource::Connect ||
            perf.transport.timeoutSource == TimeoutSource::TlsHandshake) {
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

HttpResponseParser::Callbacks makeStreamingCallbacks(
    HttpStatusCode& statusCode,
    std::unordered_map<std::string, std::string>& pendingHeaders,
    bool& hasPendingHeader,
    DataPtr& pendingBodyChunk,
    bool& hasPendingBody,
    RequestPerf& perf,
    bool& parseError,
    Error& parseErrorCode) {
    HttpResponseParser::Callbacks callbacks;
    callbacks.onStatusLine = [&statusCode](HttpStatusCode code, const std::string&) {
        statusCode = code;
    };
    callbacks.onHeaders = [&pendingHeaders, &hasPendingHeader](
                              std::unordered_map<std::string, std::string>&& headers) {
        pendingHeaders = std::move(headers);
        hasPendingHeader = true;
    };
    callbacks.onBodyData = [&pendingBodyChunk, &hasPendingBody, &perf](const Byte* data,
                                                                       size_t size) {
        if (!pendingBodyChunk) {
            pendingBodyChunk = std::make_unique<Data>();
        }
        pendingBodyChunk->append(ConstByteSpan(data, size));
        hasPendingBody = true;
        perf.parser.bodySize += static_cast<int64_t>(size);
    };
    callbacks.onError = [&parseError, &parseErrorCode](Error code) {
        parseError = true;
        parseErrorCode = code;
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
    StreamChunk exitChunk;
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
        result.exitChunk = StreamChunk::makeError(ErrorInfo{slark::Error::Cancelled});
        co_return result;
    }

    if (overallDeadline.isExpired()) {
        perf.transport.timeoutSource = TimeoutSource::ReadOverall;
        LogE("[HttpProcessor::readStreamChunk] overall deadline expired: bodySize={} url={}",
             perf.parser.bodySize, requestInfo.url);
        result.shouldExit = true;
        result.exitChunk = StreamChunk::makeError(ErrorInfo{slark::Error::ConnectionTimeout});
        co_return result;
    }

    auto socket = conn->get();
    if (!socket || !socket->isValid()) {
        LogE("[HttpProcessor::readStreamChunk] socket invalid: bodySize={} url={}",
             perf.parser.bodySize, requestInfo.url);
        result.shouldExit = true;
        result.exitChunk = StreamChunk::makeError(ErrorInfo{slark::Error::ConnectionReset});
        co_return result;
    }

    auto readResult = co_await socket->asyncReceive(readBuffer.data(), readBuffer.size(),
                                                     requestInfo.readIdleTimeout, token);
    if (!readResult.has_value()) {
        if (token.isCancelled()) {
            result.shouldExit = true;
            result.exitChunk = StreamChunk::makeError(ErrorInfo{slark::Error::Cancelled});
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
            result.exitChunk = StreamChunk::makeError(std::move(*errorInfo));
            co_return result;
        }
        LogE("[HttpProcessor::readStreamChunk] read error: err={} bodySize={} url={}",
             static_cast<int>(readResult.error()), perf.parser.bodySize, requestInfo.url);
        result.shouldExit = true;
        result.exitChunk = StreamChunk::makeError(ErrorInfo{slark::Error::ConnectionReset});
        co_return result;
    }

    if (*readResult == 0) {
        result.isEof = true;
        co_return result;
    }

    result.bytesRead = *readResult;
    co_return result;
}

HttpResponseParser::Callbacks makeResponseCallbacks(HttpResponse& response,
                                                    DataPtr& bodyData,
                                                    RequestPerf& perf,
                                                    bool& parseOk) {
    HttpResponseParser::Callbacks callbacks;
    callbacks.onStatusLine = [&response](HttpStatusCode code, const std::string& reason) {
        response.statusCode = code;
        response.reasonPhrase = reason;
    };
    callbacks.onHeaders = [&response](std::unordered_map<std::string, std::string>&& headers) {
        response.headers = std::move(headers);
    };
    callbacks.onBodyData = [&bodyData, &perf](const Byte* data, size_t size) {
        bodyData->append(ConstByteSpan(data, size));
        perf.parser.bodySize += static_cast<int64_t>(size);
    };
    callbacks.onError = [&parseOk, &perf](Error code) {
        parseOk = false;
        perf.parser.parseResult = code;
    };
    return callbacks;
}

void buildResponse(HttpResponse& response,
                   const HttpResponseParser& parser,
                   DataPtr& bodyData,
                   RequestPerf& perf,
                   Time::TimePoint requestStart) {
    response.body = bodyData->empty() ? nullptr : std::move(bodyData);
    if (parser.isHttpError()) {
        response.error =
            ErrorInfo{slark::Error::HttpError, static_cast<int32_t>(response.statusCode)};
    } else {
        response.error = ErrorInfo{slark::Error::Success};
    }
    auto requestEnd = Time::nowTimeStamp();
    perf.totalRequestTimeMs = (requestEnd - requestStart).toMilliSeconds().count();
    response.perf = perf;
}

// Determine whether a chunk triggers a redirect, returning the next request;
// returns nullopt when no redirect is needed
std::optional<RequestInfo> extractRedirectTarget(const RedirectPolicy& policy,
                                                 const StreamChunk& chunk,
                                                 const RequestInfo& currentInfo,
                                                 int32_t redirectCount) {
    if (chunk.statusCode == HttpStatusCode::Unknown || chunk.headers.empty()) {
        return std::nullopt;
    }
    HttpResponse headerResp;
    headerResp.statusCode = chunk.statusCode;
    headerResp.headers = chunk.headers;
    if (!policy.shouldRedirect(headerResp, currentInfo, redirectCount)) {
        return std::nullopt;
    }
    auto location = policy.extractRedirectLocation(chunk.headers);
    if (!location.has_value()) {
        return std::nullopt;
    }
    return RedirectPolicy::buildRedirectRequest(currentInfo, chunk.statusCode,
                                                std::move(*location));
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

Task<ResponseResult> HttpProcessor::process(const RequestInfo& requestInfo,
                                            CancellationToken token) {
    co_return co_await doRequest(requestInfo, 0, token);
}

Task<ResponseResult> HttpProcessor::doRequest(const RequestInfo& requestInfo,
                                              int32_t redirectCount,
                                              CancellationToken& token) {
    auto result = co_await executeSingle(requestInfo, token);
    if (!result.has_value()) {
        co_return result;
    }

    auto& response = result.value();
    if (redirectPolicy_.shouldRedirect(response, requestInfo, redirectCount)) {
        auto location = redirectPolicy_.extractRedirectLocation(response.headers);
        if (location.has_value()) {
            auto redirectRequest = RedirectPolicy::buildRedirectRequest(
                requestInfo, response.statusCode, std::move(location.value()));
            if (!redirectRequest.has_value()) {
                co_return std::unexpected(ErrorInfo{slark::Error::SchemeDowngradeBlocked});
            }
            co_return co_await doRequest(*redirectRequest, redirectCount + 1, token);
        }
    }

    co_return std::move(response);
}

Task<ResponseResult> HttpProcessor::executeSingle(const RequestInfo& requestInfo,
                                                  CancellationToken& token) {
    auto requestStart = Time::nowTimeStamp();
    RequestPerf perf;
    perf.host = requestInfo.url;

    LogI("[HttpProcessor::executeSingle] enter url={} method={}", requestInfo.url,
         static_cast<int>(requestInfo.methodType));

    if (token.isCancelled()) {
        co_return std::unexpected(ErrorInfo{slark::Error::Cancelled});
    }

    auto parsed = parseUrl(requestInfo.url);
    if (!parsed.isValid) {
        LogE("[HttpProcessor::executeSingle] parseUrl failed: retCode={} originCode={} url={}",
             static_cast<int>(parsed.error.retCode), parsed.error.originErrorCode, requestInfo.url);
        co_return std::unexpected(parsed.error);
    }

    auto setup =
        co_await setupConnectionAndSend("executeSingle", requestInfo, *parsed.url, perf, token);
    if (setup.hasError()) {
        co_return std::unexpected(setup.error);
    }
    auto conn = std::move(setup.conn);

    HttpResponse response;
    DataPtr bodyData = std::make_unique<Data>();
    bool parseOk = true;

    HttpResponseParser parser(makeResponseCallbacks(response, bodyData, perf, parseOk));
    parser.setPerf(&perf);
    parser.setRequestMethod(requestInfo.methodType);
    auto streamResult =
        co_await conn->asyncReceiveStreaming(parser, requestInfo.readTimeout, token);

    // Distinguish cancellation, timeout and disconnection
    if (!streamResult) {
        if (token.isCancelled()) {
            co_return std::unexpected(ErrorInfo{slark::Error::Cancelled});
        }
        LogE("[HttpProcessor::executeSingle] receive failed: timeoutSource={} url={}",
             static_cast<int>(perf.transport.timeoutSource), requestInfo.url);
        // asyncReceiveStreaming returning false means a read failure; check perf for timeout
        if (perf.transport.timeoutSource != TimeoutSource::None) {
            co_return std::unexpected(ErrorInfo{slark::Error::ConnectionTimeout});
        }
        co_return std::unexpected(ErrorInfo{slark::Error::ConnectionReset});
    }

    if (!parseOk || !parser.isCompleted()) {
        LogE("[HttpProcessor::executeSingle] parse failed: parseOk={} completed={} url={}", parseOk,
             parser.isCompleted(), requestInfo.url);
        co_return std::unexpected(ErrorInfo{slark::Error::ConnectionReset});
    }

    if (parser.shouldCloseConnection()) {
        conn->close();
    }

    buildResponse(response, parser, bodyData, perf, requestStart);

    LogI("[HttpProcessor::executeSingle] done: status={} bodySize={} elapsedMs={} url={}",
         static_cast<int>(response.statusCode), perf.parser.bodySize, perf.totalRequestTimeMs,
         requestInfo.url);

    co_return std::move(response);
}

AsyncGenerator<StreamChunk> HttpProcessor::executeStreamingSingle(const RequestInfo& requestInfo,
                                                                  CancellationToken token) {
    RequestPerf perf;
    perf.host = requestInfo.url;

    LogI("[HttpProcessor::executeStreamingSingle] enter url={} method={}", requestInfo.url,
         static_cast<int>(requestInfo.methodType));

    auto parsed = parseUrl(requestInfo.url);
    if (!parsed.isValid) {
        LogE("[HttpProcessor::executeStreamingSingle] parseUrl failed: retCode={} originCode={} "
             "url={}",
             static_cast<int>(parsed.error.retCode), parsed.error.originErrorCode, requestInfo.url);
        co_yield StreamChunk::makeError(parsed.error);
        co_return;
    }

    auto setup = co_await setupConnectionAndSend("executeStreamingSingle", requestInfo, *parsed.url,
                                                 perf, token);
    if (setup.hasError()) {
        co_yield StreamChunk::makeError(setup.error);
        co_return;
    }
    auto conn = std::move(setup.conn);

    HttpStatusCode statusCode = HttpStatusCode::Unknown;
    std::unordered_map<std::string, std::string> pendingHeaders;
    DataPtr pendingBodyChunk;
    bool hasPendingHeader = false;
    bool hasPendingBody = false;
    bool parseError = false;
    Error parseErrorCode = slark::Error::Success;

    HttpResponseParser parser(makeStreamingCallbacks(statusCode, pendingHeaders, hasPendingHeader,
                                                     pendingBodyChunk, hasPendingBody, perf,
                                                     parseError, parseErrorCode));
    parser.setPerf(&perf);
    parser.setRequestMethod(requestInfo.methodType);

    constexpr size_t kReadBufferSize = 16384;
    std::array<Byte, kReadBufferSize> readBuffer{};
    Time::Deadline overallDeadline(requestInfo.readTimeout);

    int64_t lastLoggedSize = 0;
    size_t recvLoops = 0;
    while (!parser.isCompleted() && !parser.hasError()) {
        auto chunk = co_await readStreamChunk(conn, readBuffer, overallDeadline,
                                               requestInfo, perf, token);
        if (chunk.shouldExit) {
            co_yield std::move(chunk.exitChunk);
            co_return;
        }
        if (chunk.isEof) {
            LogI("[HttpProcessor::executeStreamingSingle] EOF: bodySize={} url={}",
                 perf.parser.bodySize, requestInfo.url);
            parser.onEof();
            break;
        }
        ++recvLoops;
        parser.feed(readBuffer.data(), chunk.bytesRead);

        if (hasPendingHeader) {
            LogI("[HttpProcessor::executeStreamingSingle] header: status={} bodySize={} url={}",
                 static_cast<int>(statusCode), perf.parser.bodySize, requestInfo.url);
            co_yield StreamChunk::makeHeader(statusCode, std::move(pendingHeaders));
            hasPendingHeader = false;
            if (parser.isHttpError()) {
                co_yield StreamChunk::makeError(
                    ErrorInfo{slark::Error::HttpError, static_cast<int32_t>(statusCode)});
                co_return;
            }
        }

        if (hasPendingBody) {
            co_yield StreamChunk::makeData(std::move(pendingBodyChunk));
            pendingBodyChunk.reset();
            hasPendingBody = false;
        }

        if (perf.parser.bodySize - lastLoggedSize >= streamProgressLogBytes()) {
            LogI("[HttpProcessor::executeStreamingSingle] progress: bodySize={} recvLoops={} "
                 "url={}",
                 perf.parser.bodySize, recvLoops, requestInfo.url);
            lastLoggedSize = perf.parser.bodySize;
        }

        if (parseError) {
            LogE("[HttpProcessor::executeStreamingSingle] parse error: err={} bodySize={} url={}",
                 static_cast<int>(parseErrorCode), perf.parser.bodySize, requestInfo.url);
            co_yield StreamChunk::makeError(ErrorInfo{parseErrorCode});
            co_return;
        }
    }

    if (parser.hasError() || parseError) {
        LogE("[HttpProcessor::executeStreamingSingle] exit with parseError: err={} bodySize={} "
             "url={}",
             static_cast<int>(parseErrorCode), perf.parser.bodySize, requestInfo.url);
        co_yield StreamChunk::makeError(ErrorInfo{parseErrorCode});
    } else {
        LogI("[HttpProcessor::executeStreamingSingle] exit ok: bodySize={} recvLoops={} url={}",
             perf.parser.bodySize, recvLoops, requestInfo.url);
        co_yield StreamChunk::makeFinal();
    }

    if (parser.shouldCloseConnection()) {
        conn->close();
    }
}

AsyncGenerator<StreamChunk> HttpProcessor::processStreaming(const RequestInfo& requestInfo,
                                                            CancellationToken token) {
    LogI("[HttpProcessor::processStreaming] enter url={} method={}", requestInfo.url,
         static_cast<int>(requestInfo.methodType));

    auto checkCancelled = [&token]() -> std::optional<StreamChunk> {
        if (token.isCancelled()) {
            return StreamChunk::makeError(ErrorInfo{slark::Error::Cancelled});
        }
        return std::nullopt;
    };

    if (auto c = checkCancelled()) {
        co_yield std::move(*c);
        co_return;
    }

    // Streaming redirect orchestration (C-09): follows 301/302/307/308, limits hop count
    auto currentInfo = requestInfo;
    int32_t redirectCount = 0;
    while (true) {
        auto gen = executeStreamingSingle(currentInfo, token);
        while (true) {
            auto chunk = co_await gen.next();
            if (!chunk.has_value()) {
                co_return;
            }
            if (auto next =
                    extractRedirectTarget(redirectPolicy_, *chunk, currentInfo, redirectCount)) {
                currentInfo = std::move(*next);
                redirectCount++;
                LogI("[HttpProcessor::processStreaming] redirect to {} count={}", currentInfo.url,
                     redirectCount);
                break;  // drop the current response and issue a new request
            }
            const bool isTerminal = chunk->isFinal || chunk->hasError();
            co_yield std::move(*chunk);
            if (isTerminal) {
                co_return;
            }
        }
    }
}

}  // namespace slark::http
