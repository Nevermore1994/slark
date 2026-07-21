//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//
#include "HttpRequest.h"

#include "Log.hpp"
#include "Random.hpp"

namespace slark::http {

Coroutine<HttpResponse> HttpRequest::asyncExecute(const RequestInfo& requestInfo) {
    LogI("[HttpRequest::asyncExecute] url={} method={}",
         requestInfo.url,
         static_cast<int>(requestInfo.methodType));
    auto result = co_await processor_.process(requestInfo);

    if (result.has_value()) {
        LogI("[HttpRequest::asyncExecute] done, status={} url={}",
             static_cast<int>(result.value().statusCode),
             requestInfo.url);
        co_return std::move(result.value());
    }

    LogE("[HttpRequest::asyncExecute] failed: retCode={} originCode={} url={}",
         static_cast<int>(result.error().retCode),
         result.error().originErrorCode,
         requestInfo.url);
    HttpResponse response;
    response.error = result.error();
    co_return response;
}

Coroutine<HttpResponse> HttpRequest::asyncExecute(const RequestInfo& requestInfo,
                                                  CancellationToken token) {
    LogI("[HttpRequest::asyncExecute] url={} method={}",
         requestInfo.url,
         static_cast<int>(requestInfo.methodType));
    auto result = co_await processor_.process(requestInfo, std::move(token));

    if (result.has_value()) {
        LogI("[HttpRequest::asyncExecute] done, status={} url={}",
             static_cast<int>(result.value().statusCode),
             requestInfo.url);
        co_return std::move(result.value());
    }

    LogE("[HttpRequest::asyncExecute] failed: retCode={} originCode={} url={}",
         static_cast<int>(result.error().retCode),
         result.error().originErrorCode,
         requestInfo.url);
    HttpResponse response;
    response.error = result.error();
    co_return response;
}

void HttpRequest::asyncExecute(const RequestInfo& requestInfo, HttpResponseHandler&& handler) {
    LogI("[HttpRequest::asyncExecute(cb)] url={} method={}",
         requestInfo.url,
         static_cast<int>(requestInfo.methodType));
    auto launcher = [this](const RequestInfo& req,
                           CancellationToken token) -> Coroutine<HttpResponse> {
        co_return co_await asyncExecute(req, std::move(token));
    };

    callbackBridge_ =
        detail::startCallbackBridge(std::move(launcher), std::move(handler), requestInfo);
}

std::shared_ptr<HttpRequest> HttpRequest::create(const RequestInfo& requestInfo,
                                                 HttpResponseHandler&& handler) {
    auto request = std::make_shared<HttpRequest>();
    request->asyncExecute(requestInfo, std::move(handler));
    return request;
}

AsyncGenerator<StreamChunk> HttpRequest::streamingExecute(const RequestInfo& requestInfo,
                                                          CancellationToken token) {
    LogI("[HttpRequest::streamingExecute] url={} method={}",
         requestInfo.url,
         static_cast<int>(requestInfo.methodType));
    // 始终用 cancelToken_，保证 cancel() 与 processStreaming 联动。
    // 若用调用方传入的 token（可能为默认构造的 CancellationToken{}），cancel()
    // 无法中断 processStreaming，导致旧协程在旧 socket 上残留 Read continuation。
    // 直接返回底层 generator，避免额外一层协程帧透传。
    (void)token;
    return processor_.processStreaming(requestInfo, cancelToken_);
}

} // namespace slark::http