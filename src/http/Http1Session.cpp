//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#include "Http1Session.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <utility>

#include "HttpResponseParser.h"
#include "HttpUtil.hpp"
#include "Log.hpp"
#include "StringUtil.h"

namespace slark::http {

namespace {

constexpr auto kIdleTimeout = std::chrono::seconds(30);

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

Task<ErrorInfo> sendRequestPart(const std::shared_ptr<OwnedConnection>& connection,
                                std::string_view data,
                                const RequestInfo& requestInfo,
                                CancellationToken& token,
                                std::string_view partName) {
    if (token.isCancelled()) {
        co_return ErrorInfo{slark::Error::Cancelled};
    }

    auto result = co_await connection->asyncSend(data, requestInfo.writeTimeout, token);
    if (result) {
        co_return ErrorInfo{};
    }
    if (token.isCancelled()) {
        co_return ErrorInfo{slark::Error::Cancelled};
    }

    const auto error = mapLegacyErrorToUnified(result.error());
    LogE("[Http1Session::sendRequestPart] {} failed: error={} legacyErr={} url={}", partName,
         static_cast<int>(error), static_cast<int>(result.error()), requestInfo.url);
    co_return ErrorInfo{error};
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

std::optional<ErrorInfo> classifyReadError(Error error,
                                           bool overallExpired,
                                           RequestPerf& perf) noexcept {
    if (error != Error::ConnectionTimeout) {
        return std::nullopt;
    }
    perf.transport.timeoutSource =
        overallExpired ? TimeoutSource::ReadOverall : TimeoutSource::ReadIdle;
    return ErrorInfo{slark::Error::ConnectionTimeout};
}

}  // namespace

Http1Session::~Http1Session() {
    close();
}

bool Http1Session::Origin::matches(const Url& url) const noexcept {
    return StringUtil::iequals(scheme, url.scheme) && StringUtil::iequals(host, url.host) &&
           port == url.portNumber();
}

void Http1Session::BusyGuard::release() noexcept {
    if (session) {
        session->busy_.store(false, std::memory_order_release);
        session = nullptr;
    }
}

Http1Session::BusyGuard::~BusyGuard() {
    release();
}

std::optional<Url> Http1Session::parseUrl(const std::string& urlString, ErrorInfo& error) {
    Url url(urlString);
    if (!url.isValid()) {
        error = ErrorInfo{slark::Error::InvalidArgument, static_cast<int32_t>(url.error())};
        return std::nullopt;
    }
    if (!url.isSupportScheme()) {
        error = ErrorInfo{slark::Error::SchemeNotSupported};
        return std::nullopt;
    }
    return url;
}

Task<std::shared_ptr<OwnedConnection>> Http1Session::acquireConnection(
    const RequestInfo& requestInfo,
    const Url& url,
    RequestPerf& perf,
    CancellationToken& token) {
    std::shared_ptr<OwnedConnection> staleConnection;
    uint64_t generation = 0;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        generation = generation_;
        const auto now = std::chrono::steady_clock::now();
        const bool idleExpired = connection_ && lastActivity_.time_since_epoch().count() != 0 &&
                                 now - lastActivity_ >= kIdleTimeout;
        const bool originChanged = connection_ && (!origin_ || !origin_->matches(url));
        if (idleExpired || originChanged || (connection_ && !connection_->isValid())) {
            staleConnection = std::move(connection_);
            origin_.reset();
            pendingReadData_.clear();
        }
        if (connection_) {
            connection_->setPerf(&perf);
            co_return connection_;
        }
    }

    if (staleConnection) {
        staleConnection->close();
    }
    if (token.isCancelled()) {
        co_return nullptr;
    }

    auto result = co_await openConnection(
        url.host, url.portNumber(), url.scheme, requestInfo.ipVersion,
        requestInfo.isRedirectTarget ? requestInfo.networkAddressPolicy
                                     : NetworkAddressPolicy::AllowAny,
        requestInfo.connectTimeout, requestInfo.tlsHandshakeTimeout, &perf, token);
    if (!result) {
        co_return nullptr;
    }

    auto newConnection = std::shared_ptr<OwnedConnection>(std::move(*result));
    if (!newConnection || !newConnection->isValid()) {
        co_return nullptr;
    }

    bool discard = token.isCancelled();
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (generation != generation_) {
            discard = true;
        } else if (!connection_) {
            connection_ = newConnection;
            origin_ = Origin{url.scheme, url.host, url.portNumber()};
            lastActivity_ = std::chrono::steady_clock::now();
            connection_->setPerf(&perf);
        } else {
            newConnection.reset();
        }
    }

    if (discard) {
        newConnection->close();
        co_return nullptr;
    }
    if (newConnection) {
        co_return newConnection;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    if (!connection_ || !connection_->isValid()) {
        co_return nullptr;
    }
    connection_->setPerf(&perf);
    co_return connection_;
}

void Http1Session::discardConnection(const std::shared_ptr<OwnedConnection>& connection) noexcept {
    std::shared_ptr<OwnedConnection> toClose;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (connection_ == connection) {
            toClose = std::move(connection_);
            origin_.reset();
            pendingReadData_.clear();
            lastActivity_ = {};
        } else {
            toClose = connection;
        }
    }
    if (toClose) {
        toClose->close();
    }
}

Task<ErrorInfo> Http1Session::sendRequest(const std::shared_ptr<OwnedConnection>& connection,
                                          const RequestInfo& requestInfo,
                                          const Url& url,
                                          CancellationToken& token) {
    const auto request = Util::buildRequestString(
        requestInfo, url, requestInfo.connectionMode == HttpConnectionMode::KeepAlive);
    if (request.empty()) {
        co_return ErrorInfo{slark::Error::InvalidArgument};
    }

    auto error = co_await sendRequestPart(connection, request, requestInfo, token, "headers");
    if (error.retCode != slark::Error::Success || requestInfo.bodyEmpty()) {
        co_return error;
    }

    const std::string_view body(reinterpret_cast<const char*>(requestInfo.body->data()),
                                requestInfo.body->size());
    co_return co_await sendRequestPart(connection, body, requestInfo, token, "body");
}

Task<Http1Session::StreamReadResult> Http1Session::readStreamChunk(
    const std::shared_ptr<OwnedConnection>& connection,
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
        result.shouldExit = true;
        result.error = ErrorInfo{slark::Error::ConnectionTimeout};
        co_return result;
    }

    auto socket = connection->get();
    if (!socket || !socket->isValid()) {
        result.shouldExit = true;
        result.error = ErrorInfo{slark::Error::ConnectionReset};
        co_return result;
    }

    const auto readTimeout = std::min(requestInfo.readIdleTimeout, overallDeadline.remaining());
    auto readResult = co_await socket->asyncReceive(readBuffer.data(), readBuffer.size(), readTimeout,
                                                    token);
    if (!readResult) {
        if (token.isCancelled()) {
            result.shouldExit = true;
            result.error = ErrorInfo{slark::Error::Cancelled};
            co_return result;
        }
        auto errorInfo = classifyReadError(readResult.error(), overallDeadline.isExpired(), perf);
        result.shouldExit = true;
        result.error = errorInfo.value_or(ErrorInfo{slark::Error::ConnectionReset});
        co_return result;
    }
    if (*readResult == 0) {
        result.isEof = true;
        co_return result;
    }

    result.bytesRead = *readResult;
    co_return result;
}

AsyncGenerator<HttpResponseEvent> Http1Session::executeEvents(RequestInfo requestInfo,
                                                               CancellationToken token) {
    if (busy_.exchange(true, std::memory_order_acq_rel)) {
        co_yield HttpResponseEvent::makeError(ErrorInfo{slark::Error::InvalidOperation});
        co_return;
    }
    BusyGuard busyGuard{this};

    RequestPerf perf;
    perf.host = requestInfo.url;
    const auto requestStart = Time::nowTimeStamp();
    if (token.isCancelled()) {
        busyGuard.release();
        co_yield HttpResponseEvent::makeError(ErrorInfo{slark::Error::Cancelled}, std::move(perf));
        co_return;
    }

    ErrorInfo parseError;
    auto parsedUrl = parseUrl(requestInfo.url, parseError);
    if (!parsedUrl || !Util::validateRequest(requestInfo, *parsedUrl)) {
        busyGuard.release();
        if (!parsedUrl) {
            co_yield HttpResponseEvent::makeError(std::move(parseError), std::move(perf));
        } else {
            co_yield HttpResponseEvent::makeError(ErrorInfo{slark::Error::InvalidArgument},
                                                  std::move(perf));
        }
        co_return;
    }

    auto connection = co_await acquireConnection(requestInfo, *parsedUrl, perf, token);
    if (!connection) {
        const auto error = token.isCancelled() ? ErrorInfo{slark::Error::Cancelled}
                                                : ErrorInfo{slark::Error::ConnectionFailed};
        busyGuard.release();
        co_yield HttpResponseEvent::makeError(error, std::move(perf));
        co_return;
    }

    const auto sendError = co_await sendRequest(connection, requestInfo, *parsedUrl, token);
    if (sendError.retCode != slark::Error::Success) {
        discardConnection(connection);
        busyGuard.release();
        co_yield HttpResponseEvent::makeError(sendError, std::move(perf));
        co_return;
    }
    co_yield HttpResponseEvent::makeConnected();

    ResponseHeader header;
    bool hasHeaders = false;
    DataPtr pendingBody;
    std::unordered_map<std::string, std::string> trailers;
    bool parseErrorSeen = false;
    Error parseErrorCode = slark::Error::Success;
    HttpResponseParser parser(makeEventCallbacks(header, hasHeaders, trailers, pendingBody, perf,
                                                 parseErrorSeen, parseErrorCode));
    parser.setPerf(&perf);
    parser.setRequestMethod(requestInfo.methodType);
    parser.setMaxResponseBodySize(requestInfo.maxResponseBodySize);

    std::array<Byte, 16384> readBuffer{};
    Time::Deadline overallDeadline(requestInfo.readTimeout);
    size_t recvLoops = 0;

    while (!parser.isCompleted() && !parser.hasError()) {
        std::string pending;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            pending.swap(pendingReadData_);
        }
        if (!pending.empty()) {
            parser.feed(reinterpret_cast<const Byte*>(pending.data()), pending.size());
        } else {
            auto read = co_await readStreamChunk(connection, readBuffer, overallDeadline, requestInfo,
                                                 perf, token);
            if (read.shouldExit) {
                discardConnection(connection);
                busyGuard.release();
                co_yield HttpResponseEvent::makeError(std::move(read.error), std::move(perf));
                co_return;
            }
            if (read.isEof) {
                parser.onEof();
            } else {
                ++recvLoops;
                parser.feed(readBuffer.data(), read.bytesRead);
            }
        }

        if (hasHeaders) {
            co_yield HttpResponseEvent::makeHeaders(std::move(header));
            hasHeaders = false;
        }
        if (pendingBody) {
            co_yield HttpResponseEvent::makeData(std::move(pendingBody));
        }
        if (parseErrorSeen) {
            discardConnection(connection);
            busyGuard.release();
            co_yield HttpResponseEvent::makeError(ErrorInfo{parseErrorCode}, std::move(perf));
            co_return;
        }
        if (parser.isCompleted()) {
            break;
        }
        if (recvLoops == 0 && !parser.hasError()) {
            continue;
        }
    }

    if (parser.hasError() || parseErrorSeen || !parser.isCompleted()) {
        discardConnection(connection);
        const auto error = parseErrorCode == slark::Error::Success
                               ? ErrorInfo{slark::Error::ConnectionReset}
                               : ErrorInfo{parseErrorCode};
        busyGuard.release();
        co_yield HttpResponseEvent::makeError(error, std::move(perf));
        co_return;
    }

    if (token.isCancelled()) {
        discardConnection(connection);
        busyGuard.release();
        co_yield HttpResponseEvent::makeError(ErrorInfo{slark::Error::Cancelled}, std::move(perf));
        co_return;
    }

    perf.totalRequestTimeMs =
        (Time::nowTimeStamp() - requestStart).toMilliSeconds().count();
    const bool keepAlive = requestInfo.connectionMode == HttpConnectionMode::KeepAlive;
    const bool canReuse = keepAlive && parser.canReuseConnection();
    bool storedForReuse = false;
    if (canReuse) {
        auto unread = parser.takeUnreadData();
        std::lock_guard<std::mutex> lock(mutex_);
        if (connection_ == connection && connection_->isValid()) {
            pendingReadData_ = std::move(unread);
            lastActivity_ = std::chrono::steady_clock::now();
            storedForReuse = true;
        }
    }
    if (!storedForReuse) {
        discardConnection(connection);
    }

    if (parser.isHttpError()) {
        busyGuard.release();
        co_yield HttpResponseEvent::makeError(
            ErrorInfo{slark::Error::HttpError, static_cast<int32_t>(parser.statusCode())},
            std::move(perf));
        co_return;
    }
    busyGuard.release();
    co_yield HttpResponseEvent::makeCompleted(std::move(perf), std::move(trailers));
}

void Http1Session::close() noexcept {
    std::shared_ptr<OwnedConnection> connection;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        ++generation_;
        connection = std::move(connection_);
        origin_.reset();
        pendingReadData_.clear();
        lastActivity_ = {};
    }
    if (connection) {
        connection->close();
    }
}

}  // namespace slark::http
