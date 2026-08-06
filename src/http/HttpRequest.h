//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#pragma once

#include <memory>
#include <string>

#include "AsyncGenerator.hpp"
#include "CallbackBridge.hpp"
#include "CancellationToken.h"
#include "HttpProcessor.h"
#include "Task.hpp"
#include "Type.h"

namespace slark::http {

class HttpRequest : public NonCopyable {
 public:
    HttpRequest() = default;
    ~HttpRequest() = default;

    Task<HttpResponse> asyncExecute(const RequestInfo& requestInfo);
    Task<HttpResponse> asyncExecute(const RequestInfo& requestInfo, CancellationToken token);

    void asyncExecute(const RequestInfo& requestInfo, HttpResponseHandler&& handler);

    static std::shared_ptr<HttpRequest> create(const RequestInfo& requestInfo,
                                               HttpResponseHandler&& handler);

    AsyncGenerator<StreamChunk> streamingExecute(const RequestInfo& requestInfo,
                                                 CancellationToken token = {});

    void cancel() noexcept {
        cancelToken_.cancel();
        if (callbackBridge_) {
            callbackBridge_->cancel();
        }
    }

    // Expose cancelToken for streamingExecute callers, keeping cancel() reachable
    [[nodiscard]] const CancellationToken& cancelToken() const noexcept {
        return cancelToken_;
    }

 private:
    HttpProcessor processor_;
    CancellationToken cancelToken_;
    std::shared_ptr<detail::ICallbackBridge> callbackBridge_;
};

// Coroutine convenience function that safely manages the request lifetime
inline Task<HttpResponse> asyncRequest(const RequestInfo& requestInfo) {
    auto request = std::make_shared<HttpRequest>();
    co_return co_await request->asyncExecute(requestInfo);
}

// Streaming request convenience function returning an AsyncGenerator.
// Uses shared_ptr to manage HttpRequest, consistent with the convenience function above
// and with HttpStream/HLSStream usage.
inline AsyncGenerator<StreamChunk> asyncRequest(const RequestInfo& requestInfo,
                                                CancellationToken token = {}) {
    auto request = std::make_shared<HttpRequest>();
    auto gen = request->streamingExecute(requestInfo, token);
    while (true) {
        auto chunk = co_await gen.next();
        if (!chunk.has_value()) {
            co_return;
        }
        co_yield std::move(chunk.value());
    }
}

}  // namespace slark::http
