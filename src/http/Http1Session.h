//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#pragma once

#include <array>
#include <atomic>
#include <chrono>
#include <memory>
#include <mutex>
#include <optional>
#include <string>

#include "AsyncGenerator.hpp"
#include "ConnectionFactory.h"
#include "Executor.h"
#include "Type.h"
#include "Url.h"

namespace slark::http {

class Http1Session final : public NonCopyable, public std::enable_shared_from_this<Http1Session> {
 public:
    Http1Session() = default;
    ~Http1Session();

    // The session's close() cancels the request, so every request is linked with the closing
    // token. The overload without a token inherits the token of the awaiting coroutine.
    AsyncGenerator<HttpResponseEvent> executeEvents(RequestInfo requestInfo);
    AsyncGenerator<HttpResponseEvent> executeEvents(RequestInfo requestInfo,
                                                    CancellationToken token);

    void close() noexcept;

    // Execution domain of this session's requests. Every resume of a request coroutine is
    // dispatched here, so requests never run on a DNS worker or the IO event thread.
    // The session owns a serial domain by default (local domain wins over the caller's).
    [[nodiscard]] const std::shared_ptr<IExecutor>& executor() const noexcept {
        return executor_;
    }

    void setExecutor(std::shared_ptr<IExecutor> executor) noexcept {
        executor_ = std::move(executor);
    }

 private:
    struct Origin final {
        std::string scheme;
        std::string host;
        uint16_t port = 0;

        [[nodiscard]] bool matches(const Url& url) const noexcept;
    };

    using ConnectionResult = std::expected<std::shared_ptr<OwnedConnection>, ErrorInfo>;

    struct BusyGuard final {
        Http1Session* session = nullptr;
        void release() noexcept;
        ~BusyGuard();
    };

    AsyncGenerator<HttpResponseEvent> executeEventsImpl(RequestInfo requestInfo);

    Task<ConnectionResult> acquireConnection(const RequestInfo& requestInfo,
                                             const Url& url,
                                             RequestPerf& perf);

    void discardConnection(const std::shared_ptr<OwnedConnection>& connection) noexcept;

    static Task<ErrorInfo> sendRequest(const std::shared_ptr<OwnedConnection>& connection,
                                       const RequestInfo& requestInfo,
                                       const Url& url);

    [[nodiscard]] static std::optional<Url> parseUrl(const std::string& urlString,
                                                     ErrorInfo& error);

    static constexpr std::chrono::seconds kDefaultIdleTimeout{30};

    mutable std::mutex mutex_;
    std::shared_ptr<IExecutor> executor_ = std::make_shared<SerialExecutor>();
    std::shared_ptr<OwnedConnection> connection_;
    std::optional<Origin> origin_;
    std::string pendingReadData_;
    std::chrono::steady_clock::time_point lastActivity_{};
    // Idle limit of the cached connection; the server's Keep-Alive hint can only lower it
    std::chrono::seconds idleTimeout_ = kDefaultIdleTimeout;
    uint64_t generation_ = 0;
    CancellationToken closeToken_;
    std::atomic<bool> busy_{false};
};

}  // namespace slark::http
