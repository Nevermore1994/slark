//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//
#pragma once

#include "Socket.hpp"
#include "StringUtil.h"
#include "Type.h"
#include "Url.h"

#include <netdb.h>
#include <sstream>
#include <string_view>
#include <tuple>

namespace slark::http {
struct RequestInfo;
} // namespace slark::http

namespace slark::http::Util {

AddressInfoPtr resolveAddress(const std::string& host,
                              const std::string& port,
                              IPVersion ipVersion);

AddressInfoPtr resolveAddress(const Url& url, IPVersion ipVersion);

template <typename T>
bool parseFieldValue(const std::unordered_map<std::string, std::string>& headers,
                     const std::string& key,
                     T& value) {
    for (const auto& [k, v] : headers) {
        if (StringUtil::iequals(k, key)) {
            if constexpr (std::is_same_v<double, T> || std::is_same_v<float, T> ||
                          std::is_same_v<uint32_t, T> || std::is_same_v<int32_t, T> ||
                          std::is_same_v<uint64_t, T> || std::is_same_v<int64_t, T>) {
                auto result = StringUtil::fromChars<T>(v);
                if (!result) {
                    return false;
                }
                value = *result;
            } else if constexpr (std::is_same_v<std::string, T> ||
                                 std::is_same_v<std::string_view, T>) {
                value = v;
            } else if constexpr (std::is_same_v<bool, T>) {
                std::istringstream(v) >> std::boolalpha >> value;
            } else if constexpr (std::is_same_v<char, T>) {
                auto result = StringUtil::fromChars<int32_t>(v);
                if (!result) {
                    return false;
                }
                value = static_cast<char>(*result);
            } else if constexpr (std::is_same_v<unsigned char, T>) {
                auto result = StringUtil::fromChars<uint32_t>(v);
                if (!result) {
                    return false;
                }
                value = static_cast<unsigned char>(*result);
            } else {
                return false;
            }
            return true;
        }
    }
    return false;
}

std::string getMethodName(HttpMethodType type);
std::string base64Encode(const std::string& str);

struct ChunkParseContext {
    int64_t chunkSize = 0;
    bool isCompleted = false;
};

struct ChunkParseCallbacks {
    std::function<void(ResultCode, int32_t)> onError;
    std::function<void(DataPtr)> onData;
};

bool parseChunkData(DataPtr& data,
                    ChunkParseContext& context,
                    const ChunkParseCallbacks& callbacks) noexcept;

std::tuple<bool, int64_t> parseResponseHeader(DataView data, ResponseHeader& response) noexcept;

/// 构建HTTP请求字符串（请求行 + 请求头）
std::string buildRequestString(const RequestInfo& info,
                               const Url& url,
                               bool keepAlive = false) noexcept;

/// 从原始响应数据中解析完整的HTTP响应
struct ParsedResponse {
    HttpStatusCode statusCode = HttpStatusCode::Unknown;
    std::unordered_map<std::string, std::string> headers;
    std::string reasonPhrase;
    DataPtr body;
    bool shouldCloseConnection = false;

    HttpResponse toResponse() noexcept {
        HttpResponse response;
        response.statusCode = statusCode;
        response.headers = std::move(headers);
        response.reasonPhrase = std::move(reasonPhrase);
        response.body = std::move(body);
        response.error = ErrorInfo{ResultCode::Success, 0};
        return response;
    }
};

bool parseRawResponse(const void* data, size_t size, ParsedResponse& out) noexcept;

} // namespace slark::http::Util
