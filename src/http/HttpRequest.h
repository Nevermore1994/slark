//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <vector>

#include "AsyncGenerator.hpp"
#include "CancellationToken.h"
#include "HttpProcessor.h"
#include "Task.hpp"
#include "Type.h"

namespace slark::http {

enum class OperationStatus : uint8_t {
    Running,
    Completed,
    Cancelled,
    Closed,
    Failed,
};

struct OperationState final : std::enable_shared_from_this<OperationState> {
    CancellationToken token;

    ~OperationState();

    [[nodiscard]] OperationStatus status() const noexcept {
        return status_.load(std::memory_order_acquire);
    }

    [[nodiscard]] bool isActive() const noexcept {
        return status() == OperationStatus::Running;
    }

    void bindExternalCancellation(const CancellationToken& token) noexcept;
    void markCompleted() noexcept;
    void markFailed() noexcept;
    void cancel() noexcept;
    void close() noexcept;

 private:
    bool transition(OperationStatus expected, OperationStatus desired) noexcept;

    std::atomic<OperationStatus> status_{OperationStatus::Running};
    CancellationTokenRegistration externalCancellation_;
};

class HttpRequest : public NonCopyable {
 public:
    HttpRequest() = default;
    ~HttpRequest() = default;

    Task<HttpResponse> asyncExecute(const RequestInfo& requestInfo);
    Task<HttpResponse> asyncExecute(const RequestInfo& requestInfo, CancellationToken token);

    AsyncGenerator<StreamChunk> streamingExecute(const RequestInfo& requestInfo,
                                                 CancellationToken token = {});

    void cancel() noexcept;

 private:
    using OperationStatePtr = std::shared_ptr<OperationState>;

    static Task<HttpResponse> executeOperation(RequestInfo requestInfo,
                                               OperationStatePtr operation);
    static AsyncGenerator<StreamChunk> streamWithOperation(
        AsyncGenerator<StreamChunk> generator,
        OperationStatePtr operation);
    OperationStatePtr beginOperation() noexcept;
    void pruneInactiveOperationsLocked(
        std::vector<OperationStatePtr>* active = nullptr) noexcept;

    mutable std::mutex cancelMutex_;
    std::vector<std::weak_ptr<OperationState>> operations_;
};

}  // namespace slark::http
