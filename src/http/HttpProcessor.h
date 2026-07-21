//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#pragma once

#include "AsyncGenerator.hpp"
#include "CancellationToken.h"
#include "Channel.hpp"
#include "ConnectionPool.h"
#include "Coroutine.hpp"
#include "RedirectPolicy.h"
#include "Type.h"

namespace slark::http {

// HTTP 请求处理器：处理单次请求、重定向、性能统计
// 注意：process() 方法中的 token 参数用于控制单次请求的取消，不存储于对象成员中
class HttpProcessor : public NonCopyable {
public:
    HttpProcessor() = default;
    ~HttpProcessor() = default;

    Coroutine<ResponseResult> process(const RequestInfo& requestInfo, CancellationToken token = {});

    AsyncGenerator<StreamChunk> processStreaming(const RequestInfo& requestInfo,
                                                 CancellationToken token = {});

private:
    // 解析后的请求上下文
    struct ParsedRequest {
        std::optional<Url> url;
        bool isValid = false;
        ErrorInfo error;
    };

    Coroutine<ResponseResult> doRequest(const RequestInfo& requestInfo,
                                        int32_t redirectCount,
                                        CancellationToken& token);

    // 执行单次 HTTP 请求（连接获取、发送、接收、解析）
    Coroutine<ResponseResult> executeSingle(const RequestInfo& requestInfo,
                                            CancellationToken& token);

    // 流式执行单次请求，直接 yield 数据块
    AsyncGenerator<StreamChunk> executeStreamingSingle(const RequestInfo& requestInfo,
                                                       CancellationToken token);

    // 验证并解析 URL
    [[nodiscard]] static ParsedRequest parseUrl(const std::string& urlStr);

    // 发送请求 body
    static Coroutine<Error> sendRequestBody(PooledConnection& conn,
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

} // namespace slark::http
