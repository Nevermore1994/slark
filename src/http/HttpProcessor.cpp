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

// 建立连接并发送请求（请求头 + 请求体），executeSingle/executeStreamingSingle 共用
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
    auto conn = co_await co_acquire_connection(
        url.host, url.portNumber(), url.scheme, requestInfo.ipVersion,
        requestInfo.connectTimeout, requestInfo.tlsHandshakeTimeout, &perf, token);
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

// 判定 chunk 是否触发重定向，返回下一个请求；不触发时返回 nullopt
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

    auto setup = co_await setupConnectionAndSend("executeSingle", requestInfo, *parsed.url, perf,
                                                 token);
    if (setup.hasError()) {
        co_return std::unexpected(setup.error);
    }
    auto conn = std::move(setup.conn);

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
             static_cast<int>(perf.transport.timeoutSource), requestInfo.url);
        // asyncReceiveStreaming 返回 false 表示读取失败，需检查 perf 判断超时
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
            LogE("[HttpProcessor::executeStreamingSingle] overall deadline expired: bodySize={} "
                 "url={}",
                 perf.parser.bodySize, requestInfo.url);
            co_yield StreamChunk::makeError(ErrorInfo{slark::Error::ConnectionTimeout});
            co_return;
        }

        auto socket = conn->get();
        if (!socket || !socket->isValid()) {
            LogE("[HttpProcessor::executeStreamingSingle] socket invalid: bodySize={} url={}",
                 perf.parser.bodySize, requestInfo.url);
            co_yield StreamChunk::makeError(ErrorInfo{slark::Error::ConnectionReset});
            co_return;
        }

        auto readResult = co_await socket->asyncReceive(readBuffer.data(), readBuffer.size(),
                                                        requestInfo.readIdleTimeout, token);
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
                LogE("[HttpProcessor::executeStreamingSingle] read timeout: timeoutSource={} "
                     "bodySize={} url={}",
                     static_cast<int>(perf.transport.timeoutSource), perf.parser.bodySize,
                     requestInfo.url);
                co_yield StreamChunk::makeError(ErrorInfo{slark::Error::ConnectionTimeout});
                co_return;
            }
            // 其他错误（连接重置、断开）异常断连，不应作为正常 EOF
            LogE("[HttpProcessor::executeStreamingSingle] read error: err={} bodySize={} url={}",
                 static_cast<int>(error), perf.parser.bodySize, requestInfo.url);
            co_yield StreamChunk::makeError(ErrorInfo{slark::Error::ConnectionReset});
            co_return;
        }
        if (*readResult == 0) {
            LogI("[HttpProcessor::executeStreamingSingle] EOF: bodySize={} url={}",
                 perf.parser.bodySize, requestInfo.url);
            parser.onEof();
            break;
        }
        ++recvLoops;
        auto received = *readResult;
        parser.feed(readBuffer.data(), received);

        if (hasPendingHeader) {
            LogI("[HttpProcessor::executeStreamingSingle] header: status={} bodySize={} url={}",
                 static_cast<int>(statusCode), perf.parser.bodySize, requestInfo.url);
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
            LogI("[HttpProcessor::executeStreamingSingle] progress: bodySize={} recvLoops={} url={}",
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

    // 流式 redirect 编排（C-09）：支持 301/302/307/308 跟随，限制跳转次数
    auto currentInfo = requestInfo;
    int32_t redirectCount = 0;
    while (true) {
        auto gen = executeStreamingSingle(currentInfo, token);
        while (true) {
            auto chunk = co_await gen.next();
            if (!chunk.has_value()) {
                co_return;
            }
            if (auto next = extractRedirectTarget(redirectPolicy_, *chunk, currentInfo,
                                                  redirectCount)) {
                currentInfo = std::move(*next);
                redirectCount++;
                LogI("[HttpProcessor::processStreaming] redirect to {} count={}", currentInfo.url,
                     redirectCount);
                break;  // 丢弃当前响应，发起新请求
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
