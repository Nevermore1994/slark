//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//
#include "HttpRequest.h"

#include "Log.hpp"

namespace slark::http {

Task<HttpResponse> HttpRequest::asyncExecute(const RequestInfo& requestInfo) {
    auto operation = beginOperation();
    return executeWithToken(requestInfo, operation, operation->token);
}

Task<HttpResponse> HttpRequest::asyncExecute(const RequestInfo& requestInfo,
                                             CancellationToken token) {
    auto operation = beginOperation();
    return executeWithToken(requestInfo, operation,
                            CancellationToken::link(token, operation->token));
}

Task<HttpResponse> HttpRequest::executeWithToken(RequestInfo requestInfo,
                                                 OperationStatePtr operation,
                                                 CancellationToken token) {
    LogI("[HttpRequest::asyncExecute] url={} method={}", requestInfo.url,
         static_cast<int>(requestInfo.methodType));
    auto result = co_await HttpProcessor::process(requestInfo, std::move(token));

    if (result.has_value()) {
        LogI("[HttpRequest::asyncExecute] done, status={} url={}",
             static_cast<int>(result.value().statusCode), requestInfo.url);
        operation->active.store(false, std::memory_order_release);
        co_return std::move(result.value());
    }

    LogE("[HttpRequest::asyncExecute] failed: retCode={} originCode={} url={}",
         static_cast<int>(result.error().retCode), result.error().originErrorCode, requestInfo.url);
    HttpResponse response;
    response.error = result.error();
    operation->active.store(false, std::memory_order_release);
    co_return response;
}

HttpRequest::OperationStatePtr HttpRequest::beginOperation() noexcept {
    std::lock_guard<std::mutex> lock(cancelMutex_);
    for (auto it = operations_.begin(); it != operations_.end();) {
        auto operation = it->lock();
        if (!operation || !operation->active.load(std::memory_order_acquire)) {
            it = operations_.erase(it);
        } else {
            ++it;
        }
    }
    auto operation = std::make_shared<OperationState>();
    operations_.emplace_back(operation);
    return operation;
}

void HttpRequest::cancel() noexcept {
    std::vector<OperationStatePtr> activeOperations;
    {
        std::lock_guard<std::mutex> lock(cancelMutex_);
        for (auto it = operations_.begin(); it != operations_.end();) {
            auto operation = it->lock();
            if (!operation || !operation->active.load(std::memory_order_acquire)) {
                it = operations_.erase(it);
                continue;
            }
            activeOperations.push_back(std::move(operation));
            ++it;
        }
    }
    for (const auto& operation : activeOperations) {
        operation->token.cancel();
    }
}

AsyncGenerator<StreamChunk> HttpRequest::streamingExecute(const RequestInfo& requestInfo,
                                                          CancellationToken token) {
    LogI("[HttpRequest::streamingExecute] url={} method={}", requestInfo.url,
         static_cast<int>(requestInfo.methodType));
    auto operation = beginOperation();
    auto generator = HttpProcessor::processStreaming(
        requestInfo, CancellationToken::link(token, operation->token));
    return streamWithOperation(std::move(generator), std::move(operation));
}

AsyncGenerator<StreamChunk> HttpRequest::streamWithOperation(
    AsyncGenerator<StreamChunk> generator,
    OperationStatePtr operation) {
    try {
        while (true) {
            auto chunk = co_await generator.next();
            if (!chunk.has_value()) {
                operation->active.store(false, std::memory_order_release);
                co_return;
            }
            co_yield std::move(*chunk);
        }
    } catch (...) {
        operation->active.store(false, std::memory_order_release);
        throw;
    }
}

}  // namespace slark::http