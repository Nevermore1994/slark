//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//
#include "Http1Session.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <utility>

#include "HttpExecutionSupport.h"
#include "HttpResponseParser.h"
#include "HttpUtil.hpp"
#include "Log.hpp"
#include "StringUtil.h"

namespace slark::http {

namespace {

// Leaves a margin so the request is not sent just as the server drops the connection
constexpr auto kKeepAliveSafetyMargin = std::chrono::seconds(1);

std::chrono::seconds idleTimeoutFor(std::optional<std::chrono::seconds> serverHint,
                                    std::chrono::seconds defaultTimeout) noexcept {
    if (!serverHint) {
        return defaultTimeout;
    }
    return std::clamp(*serverHint - kKeepAliveSafetyMargin, std::chrono::seconds(0),
                      defaultTimeout);
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

Task<Http1Session::ConnectionResult> Http1Session::acquireConnection(const RequestInfo& requestInfo,
                                                                     const Url& url,
                                                                     RequestPerf& perf) {
    std::shared_ptr<OwnedConnection> staleConnection;
    uint64_t generation = 0;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        generation = generation_;
        const auto now = std::chrono::steady_clock::now();
        const bool idleExpired = connection_ && lastActivity_.time_since_epoch().count() != 0 &&
                                 now - lastActivity_ >= idleTimeout_;
        const bool originChanged = connection_ && (!origin_ || !origin_->matches(url));
        // Unread leftovers are a response that already arrived, so the connection stays in use
        const bool peerGone = connection_ && connection_->isValid() && pendingReadData_.empty() &&
                              !isIdleConnectionUsable(connection_->get()->getFd());
        if (idleExpired || originChanged || peerGone ||
            (connection_ && !connection_->isValid())) {
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
    if (co_await coro::cancellation_requested()) {
        co_return std::unexpected(ErrorInfo{slark::Error::Cancelled});
    }

    auto result = co_await openConnection(
        url.host, url.portNumber(), url.scheme, requestInfo.ipVersion,
        requestInfo.isRedirectTarget ? requestInfo.networkAddressPolicy
                                     : NetworkAddressPolicy::AllowAny,
        requestInfo.connectTimeout, requestInfo.tlsHandshakeTimeout, &perf);
    if (!result) {
        co_return std::unexpected(result.error());
    }

    auto newConnection = std::shared_ptr<OwnedConnection>(std::move(*result));
    if (!newConnection || !newConnection->isValid()) {
        co_return std::unexpected(ErrorInfo{slark::Error::ConnectionFailed});
    }

    bool discard = co_await coro::cancellation_requested();
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (generation != generation_) {
            discard = true;
        } else if (!connection_) {
            connection_ = newConnection;
            origin_ = Origin{url.scheme, url.host, url.portNumber()};
            idleTimeout_ = kDefaultIdleTimeout;
            lastActivity_ = std::chrono::steady_clock::now();
            connection_->setPerf(&perf);
        } else {
            newConnection.reset();
        }
    }

    if (discard) {
        newConnection->close();
        co_return std::unexpected(streamEndError(co_await coro::cancellation_requested()));
    }
    if (newConnection) {
        co_return newConnection;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    if (!connection_ || !connection_->isValid()) {
        co_return std::unexpected(ErrorInfo{slark::Error::ConnectionFailed});
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
            idleTimeout_ = kDefaultIdleTimeout;
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
                                          const Url& url) {
    const auto request = Util::buildRequestString(
        requestInfo, url, requestInfo.connectionMode == HttpConnectionMode::KeepAlive);
    if (request.empty()) {
        co_return ErrorInfo{slark::Error::InvalidArgument};
    }

    auto error = co_await sendRequestPart(*connection, request, requestInfo, "headers");
    if (error.retCode != slark::Error::Success || requestInfo.bodyEmpty()) {
        co_return error;
    }

    const std::string_view body(reinterpret_cast<const char*>(requestInfo.body->data()),
                                requestInfo.body->size());
    co_return co_await sendRequestPart(*connection, body, requestInfo, "body");
}

AsyncGenerator<HttpResponseEvent> Http1Session::executeEvents(RequestInfo requestInfo) {
    CancellationToken sessionToken;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        sessionToken = closeToken_;
    }
    auto events = executeEventsImpl(std::move(requestInfo));
    events.attach(SpawnEdge{}, &sessionToken);
    return events;
}

AsyncGenerator<HttpResponseEvent> Http1Session::executeEvents(RequestInfo requestInfo,
                                                              CancellationToken token) {
    CancellationToken sessionToken;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        sessionToken = closeToken_;
    }
    auto events = executeEventsImpl(std::move(requestInfo));
    events.attach(SpawnEdge{}, &sessionToken, &token);
    return events;
}

AsyncGenerator<HttpResponseEvent> Http1Session::executeEventsImpl(RequestInfo requestInfo) {
    // Keep the session alive for the whole request: close()/destruction only cancels the
    // request and retires the cached connection, so an in-flight request must not leave a
    // dangling session behind. Null for a stack-allocated session (caller must outlive it).
    const auto self = weak_from_this().lock();

    if (busy_.exchange(true, std::memory_order_acq_rel)) {
        co_yield HttpResponseEvent::makeError(ErrorInfo{slark::Error::InvalidOperation});
        co_return;
    }
    BusyGuard busyGuard{this};

    RequestPerf perf;
    perf.host = requestInfo.url;
    const auto requestStart = Time::nowTimeStamp();
    if (co_await coro::cancellation_requested()) {
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

    auto connectionResult = co_await acquireConnection(requestInfo, *parsedUrl, perf);
    if (!connectionResult) {
        busyGuard.release();
        co_yield HttpResponseEvent::makeError(connectionResult.error(), std::move(perf));
        co_return;
    }
    auto connection = std::move(*connectionResult);

    const auto sendError = co_await sendRequest(connection, requestInfo, *parsedUrl);
    if (sendError.retCode != slark::Error::Success) {
        discardConnection(connection);
        busyGuard.release();
        co_yield HttpResponseEvent::makeError(sendError, std::move(perf));
        co_return;
    }
    co_yield HttpResponseEvent::makeConnected();

    ParserEventState parserState;
    HttpResponseParser parser(makeParserCallbacks(parserState, perf));
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
            auto read = co_await http::readStreamChunk(connection, readBuffer, overallDeadline,
                                                       requestInfo, perf,
                                                       "Http1Session::readStreamChunk");
            if (read.status == StreamReadStatus::Error) {
                discardConnection(connection);
                busyGuard.release();
                co_yield HttpResponseEvent::makeError(std::move(read.error), std::move(perf));
                co_return;
            }
            if (read.status == StreamReadStatus::Eof) {
                parser.onEof();
            } else {
                ++recvLoops;
                parser.feed(readBuffer.data(), read.bytesRead);
            }
        }

        if (parserState.hasHeaders) {
            co_yield HttpResponseEvent::makeHeaders(std::move(parserState.header));
            parserState.hasHeaders = false;
        }
        if (parserState.pendingBody) {
            co_yield HttpResponseEvent::makeData(std::move(parserState.pendingBody));
        }
        if (parserState.parseError) {
            discardConnection(connection);
            busyGuard.release();
            co_yield HttpResponseEvent::makeError(ErrorInfo{parserState.parseErrorCode},
                                                  std::move(perf));
            co_return;
        }
        if (parser.isCompleted()) {
            break;
        }
        if (recvLoops == 0 && !parser.hasError()) {
            continue;
        }
    }

    if (parser.hasError() || parserState.parseError || !parser.isCompleted()) {
        discardConnection(connection);
        const auto error = parserState.parseErrorCode == slark::Error::Success
                               ? ErrorInfo{slark::Error::ConnectionReset}
                               : ErrorInfo{parserState.parseErrorCode};
        busyGuard.release();
        co_yield HttpResponseEvent::makeError(error, std::move(perf));
        co_return;
    }

    if (co_await coro::cancellation_requested()) {
        discardConnection(connection);
        busyGuard.release();
        co_yield HttpResponseEvent::makeError(ErrorInfo{slark::Error::Cancelled}, std::move(perf));
        co_return;
    }

    perf.totalRequestTimeMs = (Time::nowTimeStamp() - requestStart).toMilliSeconds().count();
    const bool keepAlive = requestInfo.connectionMode == HttpConnectionMode::KeepAlive;
    const bool canReuse = keepAlive && parser.canReuseConnection();
    bool storedForReuse = false;
    if (canReuse) {
        auto unread = parser.takeUnreadData();
        const auto idleTimeout = idleTimeoutFor(parser.keepAliveTimeout(), kDefaultIdleTimeout);
        std::lock_guard<std::mutex> lock(mutex_);
        if (connection_ == connection && connection_->isValid()) {
            pendingReadData_ = std::move(unread);
            idleTimeout_ = idleTimeout;
            lastActivity_ = std::chrono::steady_clock::now();
            storedForReuse = true;
        }
    }
    if (!storedForReuse) {
        discardConnection(connection);
    }

    busyGuard.release();
    co_yield makeResponseTerminalEvent(parser, std::move(perf), std::move(parserState.trailers));
}

void Http1Session::close() noexcept {
    CancellationToken tokenToCancel;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        ++generation_;
        connection_.reset();
        tokenToCancel = closeToken_;
        closeToken_ = CancellationToken{};
        origin_.reset();
        pendingReadData_.clear();
        lastActivity_ = {};
    }
    tokenToCancel.cancel();
}

}  // namespace slark::http
