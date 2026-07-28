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

    // 暴露 cancelToken 供 streamingExecute 调用方传入，保证 cancel() 联动可达
    [[nodiscard]] const CancellationToken& cancelToken() const noexcept {
        return cancelToken_;
    }

 private:
    HttpProcessor processor_;
    CancellationToken cancelToken_;
    std::shared_ptr<detail::ICallbackBridge> callbackBridge_;
};

// 协程便捷函数，安全管理请求生命周期
inline Task<HttpResponse> asyncRequest(const RequestInfo& requestInfo) {
    auto request = std::make_shared<HttpRequest>();
    co_return co_await request->asyncExecute(requestInfo);
}

// 流式请求便捷函数，返回 AsyncGenerator
// 用 shared_ptr 管理 HttpRequest，与上面的便捷函数及 HttpStream/HLSStream 用法保持一致
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
