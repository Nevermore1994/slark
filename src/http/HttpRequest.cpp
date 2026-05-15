//
// Created by Nevermore on 2026/4/2.
// Copyright (c) 2026 Nevermore All rights reserved.
//
#include "HttpRequest.h"

#include "Log.hpp"
#include "Random.hpp"

namespace slark::http {

Coroutine<HttpResponse> HttpRequest::asyncExecute(const RequestInfo& requestInfo) {
    auto result = co_await processor_.process(requestInfo);

    if (result.has_value()) {
        co_return std::move(result.value());
    }

    HttpResponse response;
    response.error = result.error();
    co_return response;
}

Coroutine<HttpResponse> HttpRequest::asyncExecute(const RequestInfo& requestInfo,
                                                  CancellationToken token) {
    auto result = co_await processor_.process(requestInfo, std::move(token));

    if (result.has_value()) {
        co_return std::move(result.value());
    }

    HttpResponse response;
    response.error = result.error();
    co_return response;
}

void HttpRequest::asyncExecute(const RequestInfo& requestInfo, HttpResponseHandler&& handler) {
    auto launcher = [this](const RequestInfo& req,
                           CancellationToken token) -> Coroutine<HttpResponse> {
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
    auto gen = processor_.processStreaming(requestInfo, std::move(token));
    while (true) {
        auto chunk = co_await gen.next();
        if (!chunk.has_value()) {
            co_return;
        }
        co_yield std::move(chunk.value());
    }
}

} // namespace slark::http