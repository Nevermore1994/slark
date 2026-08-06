//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//
#include "HttpRequest.h"

#include "Log.hpp"
#include "Random.hpp"

namespace slark::http {

Task<HttpResponse> HttpRequest::asyncExecute(const RequestInfo& requestInfo) {
    // Unify request cancellation state: the no-token overload forwards the internal
    // cancelToken_, ensuring cancel() can interrupt requests started by asyncExecute (C-05)
    co_return co_await asyncExecute(requestInfo, cancelToken_);
}

Task<HttpResponse> HttpRequest::asyncExecute(const RequestInfo& requestInfo,
                                             CancellationToken token) {
    LogI("[HttpRequest::asyncExecute] url={} method={}", requestInfo.url,
         static_cast<int>(requestInfo.methodType));
    auto result = co_await processor_.process(requestInfo, std::move(token));

    if (result.has_value()) {
        LogI("[HttpRequest::asyncExecute] done, status={} url={}",
             static_cast<int>(result.value().statusCode), requestInfo.url);
        co_return std::move(result.value());
    }

    LogE("[HttpRequest::asyncExecute] failed: retCode={} originCode={} url={}",
         static_cast<int>(result.error().retCode), result.error().originErrorCode, requestInfo.url);
    HttpResponse response;
    response.error = result.error();
    co_return response;
}

void HttpRequest::asyncExecute(const RequestInfo& requestInfo, HttpResponseHandler&& handler) {
    LogI("[HttpRequest::asyncExecute(cb)] url={} method={}", requestInfo.url,
         static_cast<int>(requestInfo.methodType));
    auto launcher = [this](const RequestInfo& req, CancellationToken token) -> Task<HttpResponse> {
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
    LogI("[HttpRequest::streamingExecute] url={} method={}", requestInfo.url,
         static_cast<int>(requestInfo.methodType));
    // Unify request cancellation state: combine the caller's token with the internal
    // cancelToken_; either cancels the stream, so cancel() and external tokens both work (C-04)
    return processor_.processStreaming(requestInfo, CancellationToken::link(token, cancelToken_));
}

}  // namespace slark::http