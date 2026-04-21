//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#pragma once

#include "AsyncGenerator.hpp"
#include "CallbackBridge.hpp"
#include "CancellationToken.h"
#include "Coroutine.hpp"
#include "HttpProcessor.h"
#include "Type.h"

#include <memory>
#include <string>

namespace slark::http {

// 统一 HTTP 请求接口，支持协程和回调两种模式
// 注意：实例不可并发复用，每次 execute 应使用独立实例
class HttpRequest : public NonCopyable {
public:
    HttpRequest() = default;
    ~HttpRequest() = default;

    // 协程执行请求
    Coroutine<HttpResponse> asyncExecute(const RequestInfo& requestInfo);
    Coroutine<HttpResponse> asyncExecute(const RequestInfo& requestInfo, CancellationToken token);

    // 异步执行请求，通过回调返回结果
    void asyncExecute(const RequestInfo& requestInfo, HttpResponseHandler&& handler);

    // 静态方法：创建并异步执行请求，返回 HttpRequest 实例用于取消
    static std::shared_ptr<HttpRequest> create(const RequestInfo& requestInfo,
                                               HttpResponseHandler&& handler);

    // 流式执行请求，返回 AsyncGenerator
    // 类似 C# 的 IAsyncEnumerable<T>，无需 Channel 中转
    // 使用示例：
    //   auto gen = request.streamingExecute(requestInfo);
    //   while (true) {
    //       auto chunk = co_await gen.next();
    //       if (!chunk.has_value()) break;
    //       // 处理 chunk.value()
    //   }
    AsyncGenerator<StreamChunk> streamingExecute(const RequestInfo& requestInfo,
                                                 CancellationToken token = {});

    void cancel() noexcept {
        if (callbackBridge_) {
            callbackBridge_->cancel();
        }
    }

private:
    HttpProcessor processor_;
    std::shared_ptr<detail::ICallbackBridge> callbackBridge_;
};

// 协程便捷函数，安全管理请求生命周期
inline Coroutine<HttpResponse> aysncRequest(const RequestInfo& requestInfo) {
    auto request = std::make_shared<HttpRequest>();
    co_return co_await request->asyncExecute(requestInfo);
}

// 流式请求便捷函数，返回 AsyncGenerator
inline AsyncGenerator<StreamChunk> aysncRequest(const RequestInfo& requestInfo,
                                                CancellationToken token = {}) {
    HttpRequest request;
    auto gen = request.streamingExecute(requestInfo, token);
    while (true) {
        auto chunk = co_await gen.next();
        if (!chunk.has_value()) {
            co_return;
        }
        co_yield std::move(chunk.value());
    }
}

} // namespace slark::http
