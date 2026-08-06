//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#pragma once

#include "AsyncGenerator.hpp"
#include "CancellationToken.h"
#include "ConnectionPool.h"
#include "RedirectPolicy.h"
#include "Task.hpp"
#include "Type.h"

namespace slark::http {

// HTTP request processor: handles single requests, redirects and performance stats.
// Note: the token parameter in process() controls cancellation per request and is not
// stored as an object member.
class HttpProcessor : public NonCopyable {
 public:
    HttpProcessor() = default;
    ~HttpProcessor() = default;

    Task<ResponseResult> process(RequestInfo requestInfo, CancellationToken token = {});

    AsyncGenerator<StreamChunk> processStreaming(RequestInfo requestInfo,
                                                 CancellationToken token = {});

 private:
    AsyncGenerator<HttpResponseEvent> processEvents(RequestInfo requestInfo,
                                                     CancellationToken token = {});

    struct ParsedRequest {
        std::optional<Url> url;
        bool isValid = false;
        ErrorInfo error;
    };

    Task<ResponseResult> aggregateEvents(RequestInfo requestInfo, CancellationToken token);

    AsyncGenerator<HttpResponseEvent> executeEventsSingle(RequestInfo requestInfo,
                                                          CancellationToken token);

    [[nodiscard]] static ParsedRequest parseUrl(const std::string& urlStr);

    RedirectPolicy redirectPolicy_;
};

}  // namespace slark::http
