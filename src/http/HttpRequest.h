//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#pragma once

#include <mutex>

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
    Task<HttpResponse> executeWithToken(RequestInfo requestInfo, CancellationToken token);
    CancellationToken beginOperation() noexcept;

    HttpProcessor processor_;
    mutable std::mutex cancelMutex_;
    CancellationToken cancelToken_;
};

}  // namespace slark::http
