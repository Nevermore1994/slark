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

Task<Error> HttpProcessor::sendRequestBody(PooledConnection& conn,
                                           const RequestInfo& requestInfo,
                                           CancellationToken& token) {
    if (requestInfo.bodyEmpty()) {
        co_return slark::Error::Success;
    }
    if (token.isCancelled()) {
        co_return slark::Error::Cancelled;
    }
    std::string_view bodyView(reinterpret_cast<const char*>(requestInfo.body->data()),
                              requestInfo.body->size());
    auto bodyResult = co_await conn.asyncSend(bodyView, requestInfo.writeTimeout, token);
    if (!bodyResult.has_value()) {
        if (token.isCancelled()) {
            co_return slark::Error::Cancelled;
        }
        co_return mapLegacyErrorToUnified(bodyResult.error());
    }
    co_return slark::Error::Success;
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
            auto redirectRequest =
                RedirectPolicy::buildRedirectRequest(requestInfo,
                                                     response.statusCode,
                                                     std::move(location.value()));
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

    LogI("[HttpProcessor::executeSingle] enter url={} method={}",
         requestInfo.url,
         static_cast<int>(requestInfo.methodType));

    if (token.isCancelled()) {
        co_return std::unexpected(ErrorInfo{slark::Error::Cancelled});
    }

    auto parsed = parseUrl(requestInfo.url);
    if (!parsed.isValid) {
        LogE("[HttpProcessor::executeSingle] parseUrl failed: retCode={} originCode={} url={}",
             static_cast<int>(parsed.error.retCode),
             parsed.error.originErrorCode,
             requestInfo.url);
        co_return std::unexpected(parsed.error);
    }

    if (token.isCancelled()) {
        co_return std::unexpected(ErrorInfo{slark::Error::Cancelled});
    }

    // 获取连接
    auto conn = co_await co_acquire_connection(parsed.url->host,
                                               parsed.url->portNumber(),
                                               parsed.url->scheme,
                                               requestInfo.ipVersion,
                                               requestInfo.connectTimeout,
                                               requestInfo.tlsHandshakeTimeout,
                                               &perf);
    if (!conn || !conn->isValid()) {
        LogE("[HttpProcessor::executeSingle] connect failed: timeoutSource={} url={}",
             static_cast<int>(perf.transport.timeoutSource),
             requestInfo.url);
        // 根据 perf 判断超时类型
        if (perf.transport.timeoutSource == TimeoutSource::Connect) {
            co_return std::unexpected(ErrorInfo{slark::Error::ConnectionTimeout});
        }
        if (perf.transport.timeoutSource == TimeoutSource::TlsHandshake) {
            co_return std::unexpected(ErrorInfo{slark::Error::ConnectionTimeout});
        }
        co_return std::unexpected(ErrorInfo{slark::Error::ConnectionFailed});
    }

    // 设置socket的性能统计指针
    conn->setPerf(&perf);

    if (token.isCancelled()) {
        co_return std::unexpected(ErrorInfo{slark::Error::Cancelled});
    }

    bool keepAlive = true;
    auto requestStr = Util::buildRequestString(requestInfo, *parsed.url, keepAlive);
    auto sendResult = co_await conn->asyncSend(requestStr, requestInfo.writeTimeout, token);
    if (!sendResult) {
        if (token.isCancelled()) {
            co_return std::unexpected(ErrorInfo{slark::Error::Cancelled});
        }
        LogE("[HttpProcessor::executeSingle] send failed: legacyErr={} url={}",
             static_cast<int>(sendResult.error()),
             requestInfo.url);
        co_return std::unexpected(ErrorInfo{mapLegacyErrorToUnified(sendResult.error())});
    }

    auto bodyResult = co_await sendRequestBody(*conn, requestInfo, token);
    if (bodyResult != slark::Error::Success) {
        LogE("[HttpProcessor::executeSingle] sendBody failed: err={} url={}",
             static_cast<int>(bodyResult),
             requestInfo.url);
        co_return std::unexpected(ErrorInfo{bodyResult});
    }

    if (token.isCancelled()) {
        co_return std::unexpected(ErrorInfo{slark::Error::Cancelled});
    }

    HttpResponse response;
    DataPtr bodyData = std::make_unique<Data>();
    bool parseOk = true;

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

    HttpResponseParser parser(std::move(callbacks));
    parser.setPerf(&perf);
    parser.setRequestMethod(requestInfo.methodType);
    auto streamResult =
        co_await conn->asyncReceiveStreaming(parser, requestInfo.readTimeout, token);

    // 区分取消、超时和断连
    if (!streamResult) {
        if (token.isCancelled()) {
            co_return std::unexpected(ErrorInfo{slark::Error::Cancelled});
        }
        LogE("[HttpProcessor::executeSingle] receive failed: timeoutSource={} url={}",
             static_cast<int>(perf.transport.timeoutSource),
             requestInfo.url);
        // asyncReceiveStreaming 返回 false 表示读取失败，需检查 perf 判断超时
        if (perf.transport.timeoutSource != TimeoutSource::None) {
            co_return std::unexpected(ErrorInfo{slark::Error::ConnectionTimeout});
        }
        co_return std::unexpected(ErrorInfo{slark::Error::ConnectionReset});
    }

    if (!parseOk || !parser.isCompleted()) {
        LogE("[HttpProcessor::executeSingle] parse failed: parseOk={} completed={} url={}",
             parseOk,
             parser.isCompleted(),
             requestInfo.url);
        co_return std::unexpected(ErrorInfo{slark::Error::ConnectionReset});
    }

    if (parser.shouldCloseConnection()) {
        conn->close();
    }

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

    LogI("[HttpProcessor::executeSingle] done: status={} bodySize={} elapsedMs={} url={}",
         static_cast<int>(response.statusCode),
         perf.parser.bodySize,
         perf.totalRequestTimeMs,
         requestInfo.url);

    co_return std::move(response);
}

AsyncGenerator<StreamChunk> HttpProcessor::processStreaming(const RequestInfo& requestInfo,
                                                            CancellationToken token) {
    RequestPerf perf;
    perf.host = requestInfo.url;

    LogI("[HttpProcessor::processStreaming] enter url={} method={}",
         requestInfo.url,
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

    auto parsed = parseUrl(requestInfo.url);
    if (!parsed.isValid) {
        LogE("[HttpProcessor::processStreaming] parseUrl failed: retCode={} originCode={} url={}",
             static_cast<int>(parsed.error.retCode),
             parsed.error.originErrorCode,
             requestInfo.url);
        co_yield StreamChunk::makeError(parsed.error);
        co_return;
    }

    if (auto c = checkCancelled()) {
        co_yield std::move(*c);
        co_return;
    }

    // 获取连接
    auto conn = co_await co_acquire_connection(parsed.url->host,
                                               parsed.url->portNumber(),
                                               parsed.url->scheme,
                                               requestInfo.ipVersion,
                                               requestInfo.connectTimeout,
                                               requestInfo.tlsHandshakeTimeout,
                                               &perf);
    if (!conn || !conn->isValid()) {
        LogE("[HttpProcessor::processStreaming] connect failed: timeoutSource={} url={}",
             static_cast<int>(perf.transport.timeoutSource),
             requestInfo.url);
        // 根据 perf 判断超时类型
        if (perf.transport.timeoutSource == TimeoutSource::Connect ||
            perf.transport.timeoutSource == TimeoutSource::TlsHandshake) {
            co_yield StreamChunk::makeError(ErrorInfo{slark::Error::ConnectionTimeout});
            co_return;
        }
        co_yield StreamChunk::makeError(ErrorInfo{slark::Error::ConnectionFailed});
        co_return;
    }

    conn->setPerf(&perf);

    if (auto c = checkCancelled()) {
        co_yield std::move(*c);
        co_return;
    }

    bool keepAlive = true;
    auto requestStr = Util::buildRequestString(requestInfo, *parsed.url, keepAlive);
    auto sendResult = co_await conn->asyncSend(requestStr, requestInfo.writeTimeout, token);
    if (!sendResult) {
        if (auto c = checkCancelled()) {
            co_yield std::move(*c);
            co_return;
        }
        LogE("[HttpProcessor::processStreaming] send failed: legacyErr={} url={}",
             static_cast<int>(sendResult.error()),
             requestInfo.url);
        co_yield StreamChunk::makeError(ErrorInfo{mapLegacyErrorToUnified(sendResult.error())});
        co_return;
    }

    if (!requestInfo.bodyEmpty()) {
        if (auto c = checkCancelled()) {
            co_yield std::move(*c);
            co_return;
        }
        std::string_view bodyView(reinterpret_cast<const char*>(requestInfo.body->data()),
                                  requestInfo.body->size());
        auto bodyResult = co_await conn->asyncSend(bodyView, requestInfo.writeTimeout, token);
        if (!bodyResult) {
            if (auto c = checkCancelled()) {
                co_yield std::move(*c);
                co_return;
            }
            co_yield StreamChunk::makeError(ErrorInfo{mapLegacyErrorToUnified(bodyResult.error())});
            co_return;
        }
    }

    if (auto c = checkCancelled()) {
        co_yield std::move(*c);
        co_return;
    }

    // 流式接收：parser 回调是同步的，使用待 yield 缓冲区逐段产出
    HttpStatusCode statusCode = HttpStatusCode::Unknown;
    std::unordered_map<std::string, std::string> pendingHeaders;
    DataPtr pendingBodyChunk;
    bool hasPendingHeader = false;
    bool hasPendingBody = false;
    bool parseError = false;
    Error parseErrorCode = slark::Error::Success;

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

    HttpResponseParser parser(std::move(callbacks));
    parser.setPerf(&perf);
    parser.setRequestMethod(requestInfo.methodType);

    constexpr size_t kReadBufferSize = 16384;
    std::array<Byte, kReadBufferSize> readBuffer{};

    // 使用 readTimeout 作为总时限约束，与 executeSingle 行为一致
    Time::Deadline overallDeadline(requestInfo.readTimeout);

    int64_t lastLoggedSize = 0;
    size_t recvLoops = 0;
    while (!parser.isCompleted() && !parser.hasError()) {
        if (auto c = checkCancelled()) {
            co_yield std::move(*c);
            co_return;
        }

        // 检查总时限
        if (overallDeadline.isExpired()) {
            perf.transport.timeoutSource = TimeoutSource::ReadOverall;
            LogE("[HttpProcessor::processStreaming] overall deadline expired: bodySize={} url={}",
                 perf.parser.bodySize,
                 requestInfo.url);
            co_yield StreamChunk::makeError(ErrorInfo{slark::Error::ConnectionTimeout});
            co_return;
        }

        auto socket = conn->get();
        if (!socket || !socket->isValid()) {
            LogE("[HttpProcessor::processStreaming] socket invalid: bodySize={} url={}",
                 perf.parser.bodySize,
                 requestInfo.url);
            co_yield StreamChunk::makeError(ErrorInfo{slark::Error::ConnectionReset});
            co_return;
        }

        auto readResult = co_await socket->asyncReceive(readBuffer.data(),
                                                        readBuffer.size(),
                                                        requestInfo.readIdleTimeout,
                                                        token);
        if (!readResult.has_value()) {
            if (auto c = checkCancelled()) {
                co_yield std::move(*c);
                co_return;
            }
            // 区分超时和其他错误：超时不应被当作 EOF
            auto error = readResult.error();
            if (error == Error::ConnectionTimeout) {
                if (overallDeadline.isExpired()) {
                    perf.transport.timeoutSource = TimeoutSource::ReadOverall;
                } else {
                    perf.transport.timeoutSource = TimeoutSource::ReadIdle;
                }
                LogE("[HttpProcessor::processStreaming] read timeout: timeoutSource={} bodySize={} "
                     "url={}",
                     static_cast<int>(perf.transport.timeoutSource),
                     perf.parser.bodySize,
                     requestInfo.url);
                co_yield StreamChunk::makeError(ErrorInfo{slark::Error::ConnectionTimeout});
                co_return;
            }
            // 其他错误（连接重置、断开）异常断连，不应作为正常 EOF
            LogE("[HttpProcessor::processStreaming] read error: err={} bodySize={} url={}",
                 static_cast<int>(error),
                 perf.parser.bodySize,
                 requestInfo.url);
            co_yield StreamChunk::makeError(ErrorInfo{slark::Error::ConnectionReset});
            co_return;
        }
        if (*readResult == 0) {
            LogI("[HttpProcessor::processStreaming] EOF: bodySize={} url={}",
                 perf.parser.bodySize,
                 requestInfo.url);
            parser.onEof();
            break;
        }
        ++recvLoops;
        auto received = *readResult;
        parser.feed(readBuffer.data(), received);

        if (hasPendingHeader) {
            LogI("[HttpProcessor::processStreaming] header: status={} bodySize={} url={}",
                 static_cast<int>(statusCode),
                 perf.parser.bodySize,
                 requestInfo.url);
            co_yield StreamChunk::makeHeader(statusCode, std::move(pendingHeaders));
            hasPendingHeader = false;
            // 4xx/5xx 错误响应：跳过 body 直接终止，避免读取无意义数据
            if (parser.isHttpError()) {
                co_yield StreamChunk::makeError(
                    ErrorInfo{slark::Error::HttpError, static_cast<int32_t>(statusCode)});
                co_return;
            }
        }

        if (hasPendingBody && pendingBodyChunk) {
            co_yield StreamChunk::makeData(std::move(pendingBodyChunk));
            pendingBodyChunk.reset();
            hasPendingBody = false;
        }

        // 限频进度日志：每累计 256KB 打一次
        if (perf.parser.bodySize - lastLoggedSize >= streamProgressLogBytes()) {
            LogI("[HttpProcessor::processStreaming] progress: bodySize={} recvLoops={} url={}",
                 perf.parser.bodySize,
                 recvLoops,
                 requestInfo.url);
            lastLoggedSize = perf.parser.bodySize;
        }

        if (parseError) {
            LogE("[HttpProcessor::processStreaming] parse error: err={} bodySize={} url={}",
                 static_cast<int>(parseErrorCode),
                 perf.parser.bodySize,
                 requestInfo.url);
            co_yield StreamChunk::makeError(ErrorInfo{parseErrorCode});
            co_return;
        }
    }

    if (parser.hasError() || parseError) {
        LogE("[HttpProcessor::processStreaming] exit with parseError: err={} bodySize={} url={}",
             static_cast<int>(parseErrorCode),
             perf.parser.bodySize,
             requestInfo.url);
        co_yield StreamChunk::makeError(ErrorInfo{parseErrorCode});
    } else {
        LogI("[HttpProcessor::processStreaming] exit ok: bodySize={} recvLoops={} url={}",
             perf.parser.bodySize,
             recvLoops,
             requestInfo.url);
        co_yield StreamChunk::makeFinal();
    }

    if (parser.shouldCloseConnection()) {
        conn->close();
    }
}

}  // namespace slark::http
