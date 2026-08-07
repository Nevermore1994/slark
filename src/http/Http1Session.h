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
#include "Type.h"
#include "Url.h"

namespace slark::http {

class Http1Session final : public NonCopyable, public std::enable_shared_from_this<Http1Session> {
 public:
    Http1Session() = default;
    ~Http1Session();

    AsyncGenerator<HttpResponseEvent> executeEvents(RequestInfo requestInfo,
                                                     CancellationToken token = {});

    void close() noexcept;

 private:
    struct Origin final {
        std::string scheme;
        std::string host;
        uint16_t port = 0;

        [[nodiscard]] bool matches(const Url& url) const noexcept;
    };

    struct StreamReadResult final {
        bool shouldExit = false;
        bool isEof = false;
        size_t bytesRead = 0;
        ErrorInfo error;
    };

    struct BusyGuard final {
        Http1Session* session = nullptr;
        void release() noexcept;
        ~BusyGuard();
    };

    Task<std::shared_ptr<OwnedConnection>> acquireConnection(const RequestInfo& requestInfo,
                                                             const Url& url,
                                                             RequestPerf& perf,
                                                             CancellationToken& token);

    void discardConnection(const std::shared_ptr<OwnedConnection>& connection) noexcept;

    static Task<ErrorInfo> sendRequest(const std::shared_ptr<OwnedConnection>& connection,
                                       const RequestInfo& requestInfo,
                                       const Url& url,
                                       CancellationToken& token);

    static Task<StreamReadResult> readStreamChunk(
        const std::shared_ptr<OwnedConnection>& connection,
        std::array<Byte, 16384>& readBuffer,
        const Time::Deadline& overallDeadline,
        const RequestInfo& requestInfo,
        RequestPerf& perf,
        CancellationToken& token);

    [[nodiscard]] static std::optional<Url> parseUrl(const std::string& urlString,
                                                     ErrorInfo& error);

    mutable std::mutex mutex_;
    std::shared_ptr<OwnedConnection> connection_;
    std::optional<Origin> origin_;
    std::string pendingReadData_;
    std::chrono::steady_clock::time_point lastActivity_{};
    uint64_t generation_ = 0;
    std::atomic<bool> busy_{false};
};

}  // namespace slark::http
