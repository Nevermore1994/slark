//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//
#include "HttpRequest.h"

#include "Log.hpp"

namespace slark::http {

Task<HttpResponse> HttpRequest::asyncExecute(const RequestInfo& requestInfo) {
    auto operationToken = beginOperation();
    return executeWithToken(requestInfo, std::move(operationToken));
}

Task<HttpResponse> HttpRequest::asyncExecute(const RequestInfo& requestInfo,
                                             CancellationToken token) {
    auto operationToken = beginOperation();
    return executeWithToken(requestInfo,
                            CancellationToken::link(token, std::move(operationToken)));
}

Task<HttpResponse> HttpRequest::executeWithToken(RequestInfo requestInfo,
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

CancellationToken HttpRequest::beginOperation() noexcept {
    std::lock_guard<std::mutex> lock(cancelMutex_);
    cancelToken_ = CancellationToken{};
    return cancelToken_;
}

void HttpRequest::cancel() noexcept {
    CancellationToken token;
    {
        std::lock_guard<std::mutex> lock(cancelMutex_);
        token = cancelToken_;
    }
    token.cancel();
}

AsyncGenerator<StreamChunk> HttpRequest::streamingExecute(const RequestInfo& requestInfo,
                                                          CancellationToken token) {
    LogI("[HttpRequest::streamingExecute] url={} method={}", requestInfo.url,
         static_cast<int>(requestInfo.methodType));
    auto operationToken = beginOperation();
    return processor_.processStreaming(requestInfo,
                                       CancellationToken::link(token, std::move(operationToken)));
}

}  // namespace slark::http