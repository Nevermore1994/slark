//
// Created by Nevermore- on 2025/6/23.
//

#pragma once
#include <string_view>
#include <iostream>
#include <sstream>
#include <tuple>
#include "public/Type.h"

namespace slark::http {
struct RequestInfo;
struct Url;
}

namespace slark::http::Util {

template <typename T>
bool parseFieldValue(const std::unordered_map<std::string, std::string>& headers, const std::string& key, T& value) {
    if (headers.count(key) == 0) {
        return false;
    }
    if constexpr (std::is_same_v<double, T>) {
        value = std::stod(headers.at(key));
    } else if constexpr (std::is_same_v<float, T>) {
        value = std::stof(headers.at(key));
    } else if constexpr (std::is_same_v<uint32_t, T>) {
        value = std::stoul(headers.at(key));
    } else if constexpr (std::is_same_v<int32_t, T>) {
        value = std::stoi(headers.at(key));
    } else if constexpr (std::is_same_v<uint64_t, T>) {
        value = std::stoull(headers.at(key));
    } else if constexpr (std::is_same_v<int64_t, T>) {
        value = std::stoll(headers.at(key));
    } else if constexpr (std::is_same_v<std::string, T> || std::is_same_v<std::string_view, T>) {
        value = headers.at(key);
    } else if constexpr (std::is_same_v<bool, T>) {
        std::istringstream(headers.at(key)) >> std::boolalpha >> value;
    } else if constexpr (std::is_same_v<char, T>) {
        value = static_cast<char>(std::stoi(headers.at(key)));
    } else if constexpr (std::is_same_v<unsigned char, T>) {
        value = static_cast<unsigned char>(std::stoi(headers.at(key)));
    } else {
        return false;
    }
    return true;
}

std::string getMethodName(HttpMethodType type);

std::string base64Encode(const std::string& str);

std::string htmlEncode(
    RequestInfo& info,
    const Url& url
) noexcept;

bool parseChunkHandler(
    DataPtr& data,
    int64_t& chunkSize,
    bool& isCompleted,
    const std::function<void(ResultCode, int32_t)>& errorHandler,
    const std::function<void(DataPtr)>& dataHandler
) noexcept;

std::tuple<bool, int64_t> parseResponseHeader(
    DataView data,
    ResponseHeader& response
) noexcept;

} // slark

