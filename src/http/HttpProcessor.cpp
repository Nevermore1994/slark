//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//
#include "HttpProcessor.h"

#include "Clock.h"
#include "ConnectionPool.h"
#include "CoroutineScheduler.hpp"
#include "HttpResponseParser.h"
#include "HttpUtil.hpp"
#include "Url.h"

namespace slark::http {

namespace {

ResultCode mapErrorToResultCode(Error error) noexcept {
    switch (error) {
        case Error::ConnectionTimeout:
            return ResultCode::Timeout;
        case Error::Cancelled:
            return ResultCode::Cancelled;
        case Error::ConnectionReset:
        case Error::ConnectionRefused:
        case Error::BrokenPipe:
        case Error::SocketClosed:
        case Error::SocketError:
        case Error::ConnectionFailed:
            return ResultCode::Disconnected;
        default:
            return ResultCode::Failed;
    }
}

} // namespace

HttpProcessor::ParsedRequest HttpProcessor::parseUrl(const std::string& urlStr) {
    ParsedRequest result;
    auto url = Url(urlStr);
    if (!url.isValid()) {
        result.error = ErrorInfo{ResultCode::UrlInvalid, static_cast<int32_t>(url.error())};
        return result;
    }
    if (!url.isSupportScheme()) {
        result.error = ErrorInfo{ResultCode::SchemeNotSupported, 0};
        return result;
    }
    result.url = std::move(url);
    result.isValid = true;
    return result;
}

Coroutine<ResultCode> HttpProcessor::sendRequestBody(PooledConnection& conn,
                                                     const RequestInfo& requestInfo,
                                                     CancellationToken& token) {
    if (requestInfo.bodyEmpty()) {
        co_return ResultCode::Success;
    }
    if (token.isCancelled()) {
        co_return ResultCode::Cancelled;
    }
    std::string_view bodyView(reinterpret_cast<const char*>(requestInfo.body->data()),
                              requestInfo.body->size());
    auto bodyResult = co_await conn.asyncSend(bodyView, requestInfo.writeTimeout, token);
    if (!bodyResult.has_value()) {
        if (token.isCancelled()) {
            co_return ResultCode::Cancelled;
        }
        co_return mapErrorToResultCode(bodyResult.error());
    }
    co_return ResultCode::Success;
}

Coroutine<ResponseResult> HttpProcessor::process(const RequestInfo& requestInfo,
                                                 CancellationToken token) {
    co_return co_await doRequest(requestInfo, 0, token);
}

Coroutine<ResponseResult> HttpProcessor::doRequest(const RequestInfo& requestInfo,
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
                // scheme downgrade 被拒绝
                co_return std::unexpected(ErrorInfo{ResultCode::SchemeDowngradeBlocked, 0});
            }
            co_return co_await doRequest(*redirectRequest, redirectCount + 1, token);
        }
    }

    co_return std::move(response);
}

Coroutine<ResponseResult> HttpProcessor::executeSingle(const RequestInfo& requestInfo,
                                                       CancellationToken& token) {
    auto requestStart = Time::nowTimeStamp();
    RequestPerf perf;
    perf.host = requestInfo.url;

    if (token.isCancelled()) {
        co_return std::unexpected(ErrorInfo{ResultCode::Cancelled, 0});
    }

    auto parsed = parseUrl(requestInfo.url);
    if (!parsed.isValid) {
        co_return std::unexpected(parsed.error);
    }

    if (token.isCancelled()) {
        co_return std::unexpected(ErrorInfo{ResultCode::Cancelled, 0});
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
        // 根据 perf 判断超时类型
        if (perf.transport.timeoutSource == TimeoutSource::Connect) {
            co_return std::unexpected(ErrorInfo{ResultCode::Timeout, 0});
        }
        if (perf.transport.timeoutSource == TimeoutSource::TlsHandshake) {
            co_return std::unexpected(ErrorInfo{ResultCode::Timeout, 0});
        }
        co_return std::unexpected(ErrorInfo{ResultCode::ConnectAddressError, 0});
    }

    // 设置socket的性能统计指针
    conn->setPerf(&perf);

    if (token.isCancelled()) {
        co_return std::unexpected(ErrorInfo{ResultCode::Cancelled, 0});
    }

    // 发送请求
    bool keepAlive = true;
    auto requestStr = Util::buildRequestString(requestInfo, *parsed.url, keepAlive);
    auto sendResult = co_await conn->asyncSend(requestStr, requestInfo.writeTimeout, token);
    if (!sendResult) {
        if (token.isCancelled()) {
            co_return std::unexpected(ErrorInfo{ResultCode::Cancelled, 0});
        }
        co_return std::unexpected(ErrorInfo{mapErrorToResultCode(sendResult.error()), 0});
    }

    // 发送 body
    auto bodyResult = co_await sendRequestBody(*conn, requestInfo, token);
    if (bodyResult != ResultCode::Success) {
        co_return std::unexpected(ErrorInfo{bodyResult, 0});
    }

    if (token.isCancelled()) {
        co_return std::unexpected(ErrorInfo{ResultCode::Cancelled, 0});
    }

    // 流式接收并解析响应
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
    callbacks.onError = [&parseOk, &perf](ResultCode code) {
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
            co_return std::unexpected(ErrorInfo{ResultCode::Cancelled, 0});
        }
        // asyncReceiveStreaming 返回 false 表示读取失败，需检查 perf 判断超时
        if (perf.transport.timeoutSource != TimeoutSource::None) {
            co_return std::unexpected(ErrorInfo{ResultCode::Timeout, 0});
        }
        co_return std::unexpected(ErrorInfo{ResultCode::Disconnected, 0});
    }

    if (!parseOk || !parser.isCompleted()) {
        co_return std::unexpected(ErrorInfo{ResultCode::Disconnected, 0});
    }

    // 服务端要求关闭连接时，主动关闭
    if (parser.shouldCloseConnection()) {
        conn->close();
    }

    response.body = bodyData->empty() ? nullptr : std::move(bodyData);
    response.error = ErrorInfo{ResultCode::Success, 0};

    // 记录总耗时
    auto requestEnd = Time::nowTimeStamp();
    perf.totalRequestTimeMs = (requestEnd - requestStart).toMilliSeconds().count();
    response.perf = perf;

    co_return std::move(response);
}

AsyncGenerator<StreamChunk> HttpProcessor::processStreaming(const RequestInfo& requestInfo,
                                                            CancellationToken token) {
    RequestPerf perf;
    perf.host = requestInfo.url;

    if (token.isCancelled()) {
        co_yield StreamChunk::makeError(ErrorInfo{ResultCode::Cancelled, 0});
        co_return;
    }

    auto parsed = parseUrl(requestInfo.url);
    if (!parsed.isValid) {
        co_yield StreamChunk::makeError(parsed.error);
        co_return;
    }

    if (token.isCancelled()) {
        co_yield StreamChunk::makeError(ErrorInfo{ResultCode::Cancelled, 0});
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
        // 根据 perf 判断超时类型
        if (perf.transport.timeoutSource == TimeoutSource::Connect ||
            perf.transport.timeoutSource == TimeoutSource::TlsHandshake) {
            co_yield StreamChunk::makeError(ErrorInfo{ResultCode::Timeout, 0});
            co_return;
        }
        co_yield StreamChunk::makeError(ErrorInfo{ResultCode::ConnectAddressError, 0});
        co_return;
    }

    conn->setPerf(&perf);

    if (token.isCancelled()) {
        co_yield StreamChunk::makeError(ErrorInfo{ResultCode::Cancelled, 0});
        co_return;
    }

    // 发送请求
    bool keepAlive = true;
    auto requestStr = Util::buildRequestString(requestInfo, *parsed.url, keepAlive);
    auto sendResult = co_await conn->asyncSend(requestStr, requestInfo.writeTimeout, token);
    if (!sendResult) {
        if (token.isCancelled()) {
            co_yield StreamChunk::makeError(ErrorInfo{ResultCode::Cancelled, 0});
            co_return;
        }
        co_yield StreamChunk::makeError(ErrorInfo{mapErrorToResultCode(sendResult.error()), 0});
        co_return;
    }

    // 发送 body
    if (!requestInfo.bodyEmpty()) {
        if (token.isCancelled()) {
            co_yield StreamChunk::makeError(ErrorInfo{ResultCode::Cancelled, 0});
            co_return;
        }
        std::string_view bodyView(reinterpret_cast<const char*>(requestInfo.body->data()),
                                  requestInfo.body->size());
        auto bodyResult = co_await conn->asyncSend(bodyView, requestInfo.writeTimeout, token);
        if (!bodyResult) {
            if (token.isCancelled()) {
                co_yield StreamChunk::makeError(ErrorInfo{ResultCode::Cancelled, 0});
                co_return;
            }
            co_yield StreamChunk::makeError(ErrorInfo{mapErrorToResultCode(bodyResult.error()), 0});
            co_return;
        }
    }

    if (token.isCancelled()) {
        co_yield StreamChunk::makeError(ErrorInfo{ResultCode::Cancelled, 0});
        co_return;
    }

    // 流式接收：在协程中直接读取网络数据，增量解析并 yield
    // 使用待 yield 数据缓冲区，因为 parser 回调是同步的
    HttpStatusCode statusCode = HttpStatusCode::Unknown;
    std::unordered_map<std::string, std::string> pendingHeaders;
    DataPtr pendingBodyChunk;
    bool hasPendingHeader = false;
    bool hasPendingBody = false;
    bool parseError = false;
    ResultCode parseErrorCode = ResultCode::Success;

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
    callbacks.onError = [&parseError, &parseErrorCode](ResultCode code) {
        parseError = true;
        parseErrorCode = code;
    };

    HttpResponseParser parser(std::move(callbacks));
    parser.setPerf(&perf);
    parser.setRequestMethod(requestInfo.methodType);

    // 增量读取并解析
    constexpr size_t kReadBufferSize = 16384;
    std::array<Byte, kReadBufferSize> readBuffer{};

    // 使用 readTimeout 作为总时限约束，与 executeSingle 行为一致
    Time::Deadline overallDeadline(requestInfo.readTimeout);

    while (!parser.isCompleted() && !parser.hasError()) {
        if (token.isCancelled()) {
            co_yield StreamChunk::makeError(ErrorInfo{ResultCode::Cancelled, 0});
            co_return;
        }

        // 检查总时限
        if (overallDeadline.isExpired()) {
            perf.transport.timeoutSource = TimeoutSource::ReadOverall;
            co_yield StreamChunk::makeError(ErrorInfo{ResultCode::Timeout, 0});
            co_return;
        }

        // 读取数据（使用底层的 awaitable）
        auto socket = conn->get();
        if (!socket || !socket->isValid()) {
            co_yield StreamChunk::makeError(ErrorInfo{ResultCode::Disconnected, 0});
            co_return;
        }

        // 读取数据（带取消支持），使用 readIdleTimeout 作为每次读取的空闲超时
        auto readResult = co_await socket->asyncReceive(readBuffer.data(),
                                                        readBuffer.size(),
                                                        requestInfo.readIdleTimeout,
                                                        token);
        if (!readResult.has_value()) {
            if (token.isCancelled()) {
                co_yield StreamChunk::makeError(ErrorInfo{ResultCode::Cancelled, 0});
                co_return;
            }
            // 区分超时和其他错误：超时不应被当作 EOF
            auto error = readResult.error();
            if (error == Error::ConnectionTimeout) {
                // 区分是空闲超时还是总时限超时
                if (overallDeadline.isExpired()) {
                    perf.transport.timeoutSource = TimeoutSource::ReadOverall;
                } else {
                    perf.transport.timeoutSource = TimeoutSource::ReadIdle;
                }
                co_yield StreamChunk::makeError(ErrorInfo{ResultCode::Timeout, 0});
                co_return;
            }
            // 其他错误（如连接重置、断开）都是异常断连，不应当作正常 EOF
            co_yield StreamChunk::makeError(ErrorInfo{ResultCode::Disconnected, 0});
            co_return;
        }
        if (*readResult == 0) {
            // 真正的 EOF（对端正常关闭连接）
            parser.onEof();
            break;
        }

        // 喂给解析器
        parser.feed(readBuffer.data(), *readResult);
        // 检查是否有 headers 需要 yield
        if (hasPendingHeader) {
            co_yield StreamChunk::makeHeader(statusCode, std::move(pendingHeaders));
            hasPendingHeader = false;
        }

        // 检查是否有 body 数据需要 yield
        if (hasPendingBody && pendingBodyChunk) {
            co_yield StreamChunk::makeData(std::move(pendingBodyChunk));
            pendingBodyChunk.reset();
            hasPendingBody = false;
        }

        if (parseError) {
            co_yield StreamChunk::makeError(ErrorInfo{parseErrorCode, 0});
            co_return;
        }
    }

    // 最终状态
    if (parser.hasError() || parseError) {
        co_yield StreamChunk::makeError(ErrorInfo{parseErrorCode, 0});
    } else {
        co_yield StreamChunk::makeFinal();
    }

    // 服务端要求关闭连接时，主动关闭
    if (parser.shouldCloseConnection()) {
        conn->close();
    }
}

} // namespace slark::http
