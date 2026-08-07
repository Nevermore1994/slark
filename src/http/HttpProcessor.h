//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#pragma once

#include "AsyncGenerator.hpp"
#include "CancellationToken.h"
#include "ConnectionFactory.h"
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

    static Task<ResponseResult> process(RequestInfo requestInfo, CancellationToken token = {});

    static AsyncGenerator<StreamChunk> processStreaming(RequestInfo requestInfo,
                                                         CancellationToken token = {});

 private:
    static AsyncGenerator<HttpResponseEvent> processEvents(RequestInfo requestInfo,
                                                           CancellationToken token = {});

    struct ParsedRequest {
        std::optional<Url> url;
        bool isValid = false;
        ErrorInfo error;
    };

    static Task<ResponseResult> aggregateEvents(RequestInfo requestInfo, CancellationToken token);

    static AsyncGenerator<HttpResponseEvent> executeEventsSingle(RequestInfo requestInfo,
                                                                 CancellationToken token);

    [[nodiscard]] static ParsedRequest parseUrl(const std::string& urlStr);

};

}  // namespace slark::http
