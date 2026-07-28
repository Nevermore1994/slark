//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#pragma once

#include "AsyncGenerator.hpp"
#include "CancellationToken.h"
#include "Channel.hpp"
#include "ConnectionPool.h"
#include "RedirectPolicy.h"
#include "Task.hpp"
#include "Type.h"

namespace slark::http {

// HTTP 请求处理器：处理单次请求、重定向、性能统计
// 注意：process() 方法中的 token 参数用于控制单次请求的取消，不存储于对象成员中
class HttpProcessor : public NonCopyable {
 public:
    HttpProcessor() = default;
    ~HttpProcessor() = default;

    Task<ResponseResult> process(const RequestInfo& requestInfo, CancellationToken token = {});

    AsyncGenerator<StreamChunk> processStreaming(const RequestInfo& requestInfo,
                                                 CancellationToken token = {});

 private:
    struct ParsedRequest {
        std::optional<Url> url;
        bool isValid = false;
        ErrorInfo error;
    };

    Task<ResponseResult> doRequest(const RequestInfo& requestInfo,
                                   int32_t redirectCount,
                                   CancellationToken& token);

    Task<ResponseResult> executeSingle(const RequestInfo& requestInfo, CancellationToken& token);

    AsyncGenerator<StreamChunk> executeStreamingSingle(const RequestInfo& requestInfo,
                                                       CancellationToken token);

    [[nodiscard]] static ParsedRequest parseUrl(const std::string& urlStr);

    static Task<Error> sendRequestBody(PooledConnection& conn,
                                       const RequestInfo& requestInfo,
                                       CancellationToken& token);

    RedirectPolicy redirectPolicy_;
};

inline AsyncGenerator<StreamChunk> channelToGenerator(ReceiverPtr<StreamChunk> receiver) {
    while (true) {
        auto result = receiver->receive();
        if (!result.has_value()) {
            co_return;
        }
        auto chunk = std::move(result.value());
        const bool isFinal = chunk.isFinal || chunk.hasError();
        co_yield std::move(chunk);
        if (isFinal) {
            co_return;
        }
    }
}

}  // namespace slark::http
