//
// Created by Nevermore- on 2025/6/23.
//
#pragma once

#include "Socket.hpp"
#include "StringUtil.h"
#include "Type.hpp"
#include "Url.hpp"

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
            try {
                if constexpr (std::is_same_v<double, T>) {
                    value = std::stod(v);
                } else if constexpr (std::is_same_v<float, T>) {
                    value = std::stof(v);
                } else if constexpr (std::is_same_v<uint32_t, T>) {
                    value = static_cast<uint32_t>(std::stoul(v));
                } else if constexpr (std::is_same_v<int32_t, T>) {
                    value = std::stoi(v);
                } else if constexpr (std::is_same_v<uint64_t, T>) {
                    value = std::stoull(v);
                } else if constexpr (std::is_same_v<int64_t, T>) {
                    value = std::stoll(v);
                } else if constexpr (std::is_same_v<std::string, T> ||
                                     std::is_same_v<std::string_view, T>) {
                    value = v;
                } else if constexpr (std::is_same_v<bool, T>) {
                    std::istringstream(v) >> std::boolalpha >> value;
                } else if constexpr (std::is_same_v<char, T>) {
                    value = static_cast<char>(std::stoi(v));
                } else if constexpr (std::is_same_v<unsigned char, T>) {
                    value = static_cast<unsigned char>(std::stoi(v));
                } else {
                    return false;
                }
                return true;
            } catch (...) {
                return false;
            }
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
