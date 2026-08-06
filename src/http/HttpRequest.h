//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#pragma once

#include <atomic>
#include <memory>
#include <mutex>
#include <vector>

#include "AsyncGenerator.hpp"
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

    AsyncGenerator<StreamChunk> streamingExecute(const RequestInfo& requestInfo,
                                                 CancellationToken token = {});

    void cancel() noexcept;

 private:
    struct OperationState final {
        CancellationToken token;
        std::atomic<bool> active{true};
    };
    using OperationStatePtr = std::shared_ptr<OperationState>;

    static Task<HttpResponse> executeWithToken(RequestInfo requestInfo,
                                               OperationStatePtr operation,
                                               CancellationToken token);
    static AsyncGenerator<StreamChunk> streamWithOperation(
        AsyncGenerator<StreamChunk> generator,
        OperationStatePtr operation);
    OperationStatePtr beginOperation() noexcept;

    mutable std::mutex cancelMutex_;
    std::vector<std::weak_ptr<OperationState>> operations_;
};

}  // namespace slark::http
