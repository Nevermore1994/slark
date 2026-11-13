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

// Note: the token overloads bind the caller's token to the request and are not stored as an
// object member. The overloads without a token inherit the token of the awaiting coroutine.
class HttpProcessor : public NonCopyable {
 public:
    HttpProcessor() = default;
    ~HttpProcessor() = default;

    static Task<ResponseResult> process(RequestInfo requestInfo);
    static Task<ResponseResult> process(RequestInfo requestInfo, CancellationToken token);

    static AsyncGenerator<StreamChunk> processStreaming(RequestInfo requestInfo);
    static AsyncGenerator<StreamChunk> processStreaming(RequestInfo requestInfo,
                                                        CancellationToken token);

 private:
    static AsyncGenerator<HttpResponseEvent> processEvents(RequestInfo requestInfo);

    struct ParsedRequest {
        std::optional<Url> url;
        bool isValid = false;
        ErrorInfo error;
    };

    static Task<ResponseResult> collectResponse(RequestInfo requestInfo);

    static AsyncGenerator<HttpResponseEvent> executeEventsSingle(RequestInfo requestInfo);

    [[nodiscard]] static ParsedRequest parseUrl(const std::string& urlStr);

};

}  // namespace slark::http
