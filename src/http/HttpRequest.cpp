//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//
#include "HttpRequest.h"

#include "Log.hpp"

namespace slark::http {

namespace {

void settleOperationError(const std::shared_ptr<OperationState>& operation,
                          const ErrorInfo& error) noexcept {
    if (error.retCode == slark::Error::Cancelled || operation->token.isCancelled()) {
        operation->cancel();
    } else {
        operation->markFailed();
    }
}

void settleOperationException(const std::shared_ptr<OperationState>& operation) noexcept {
    if (operation->token.isCancelled()) {
        operation->cancel();
    } else {
        operation->markFailed();
    }
}

}  // namespace

bool OperationState::transition(OperationStatus expected, OperationStatus desired) noexcept {
    return status_.compare_exchange_strong(
        expected, desired, std::memory_order_acq_rel, std::memory_order_acquire);
}

OperationState::~OperationState() {
    close();
}

void OperationState::bindExternalCancellation(const CancellationToken& externalToken) noexcept {
    if (externalToken.isCancelled()) {
        cancel();
        return;
    }

    const auto weakOperation = weak_from_this();
    externalCancellation_ = externalToken.registerCallback([weakOperation]() {
        if (auto operation = weakOperation.lock()) {
            operation->cancel();
        }
    });
    if (!externalCancellation_.isValid() && externalToken.isCancelled()) {
        cancel();
    }
}

void OperationState::markCompleted() noexcept {
    transition(OperationStatus::Running, OperationStatus::Completed);
}

void OperationState::markFailed() noexcept {
    transition(OperationStatus::Running, OperationStatus::Failed);
}

void OperationState::cancel() noexcept {
    if (transition(OperationStatus::Running, OperationStatus::Cancelled)) {
        token.cancel();
    }
}

void OperationState::close() noexcept {
    if (transition(OperationStatus::Running, OperationStatus::Closed)) {
        token.cancel();
    }
}

Task<HttpResponse> HttpRequest::asyncExecute(const RequestInfo& requestInfo) {
    auto operation = beginOperation();
    auto task = executeOperation(requestInfo, operation);
    task.attach(SpawnEdge{}, &operation->token);
    return task;
}

Task<HttpResponse> HttpRequest::asyncExecute(const RequestInfo& requestInfo,
                                             CancellationToken token) {
    auto operation = beginOperation();
    operation->bindExternalCancellation(token);
    auto task = executeOperation(requestInfo, operation);
    task.attach(SpawnEdge{}, &operation->token, &token);
    return task;
}

Task<HttpResponse> HttpRequest::executeOperation(RequestInfo requestInfo,
                                                 OperationStatePtr operation) {
    LogI("[HttpRequest::asyncExecute] url={} method={}", requestInfo.url,
         static_cast<int>(requestInfo.methodType));
    try {
        auto result = co_await HttpProcessor::process(requestInfo);

        if (result.has_value()) {
            LogI("[HttpRequest::asyncExecute] done, status={} url={}",
                 static_cast<int>(result.value().statusCode), requestInfo.url);
            operation->markCompleted();
            co_return std::move(result.value());
        }

        LogE("[HttpRequest::asyncExecute] failed: retCode={} originCode={} url={}",
             toString(result.error().retCode), result.error().originErrorCode,
             requestInfo.url);
        HttpResponse response;
        response.error = result.error();
        settleOperationError(operation, result.error());
        co_return response;
    } catch (...) {
        settleOperationException(operation);
        throw;
    }
}

void HttpRequest::pruneInactiveOperationsLocked(
    std::vector<OperationStatePtr>* active) noexcept {
    for (auto it = operations_.begin(); it != operations_.end();) {
        auto operation = it->lock();
        if (!operation || !operation->isActive()) {
            it = operations_.erase(it);
            continue;
        }
        if (active != nullptr) {
            active->push_back(std::move(operation));
        }
        ++it;
    }
}

HttpRequest::OperationStatePtr HttpRequest::beginOperation() noexcept {
    std::lock_guard<std::mutex> lock(cancelMutex_);
    pruneInactiveOperationsLocked();
    auto operation = std::make_shared<OperationState>();
    operations_.emplace_back(operation);
    return operation;
}

void HttpRequest::cancel() noexcept {
    std::vector<OperationStatePtr> activeOperations;
    {
        std::lock_guard<std::mutex> lock(cancelMutex_);
        pruneInactiveOperationsLocked(&activeOperations);
    }
    for (const auto& operation : activeOperations) {
        operation->cancel();
    }
}

AsyncGenerator<StreamChunk> HttpRequest::streamingExecute(const RequestInfo& requestInfo,
                                                          CancellationToken token) {
    LogI("[HttpRequest::streamingExecute] url={} method={}", requestInfo.url,
         static_cast<int>(requestInfo.methodType));
    auto operation = beginOperation();
    operation->bindExternalCancellation(token);
    auto generator = HttpProcessor::processStreaming(requestInfo);
    generator.attach(SpawnEdge{}, &operation->token, &token);
    return streamWithOperation(std::move(generator), std::move(operation));
}

AsyncGenerator<StreamChunk> HttpRequest::streamWithOperation(
    AsyncGenerator<StreamChunk> generator,
    OperationStatePtr operation) {
    try {
        while (true) {
            auto chunk = co_await generator.next();
            if (!chunk.has_value()) {
                if (operation->token.isCancelled()) {
                    operation->cancel();
                } else {
                    operation->markCompleted();
                }
                co_return;
            }
            if (chunk->hasError()) {
                settleOperationError(operation, chunk->error);
            }
            co_yield std::move(*chunk);
        }
    } catch (...) {
        settleOperationException(operation);
        throw;
    }
}

}  // namespace slark::http
