//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#include "HttpUtil.hpp"

#include <charconv>
#include <iterator>

#include "StringUtil.h"

namespace slark::http::Util {

namespace {

constexpr std::string_view kMethodNames[] = {"Unknown", "GET",    "POST",   "PUT",
                                             "PATCH",   "DELETE", "OPTIONS", "HEAD"};

constexpr std::string_view kCRLF = "\r\n"sv;

}  // anonymous namespace

std::string getMethodName(HttpMethodType type) {
    auto index = static_cast<size_t>(type);
    return std::string(index < std::size(kMethodNames) ? kMethodNames[index] : kMethodNames[0]);
}

std::optional<Url> parseRequestUrl(const std::string& urlString, ErrorInfo& error) {
    Url url(urlString);
    if (!url.isValid()) {
        error = ErrorInfo{slark::Error::InvalidArgument, static_cast<int32_t>(url.error())};
        return std::nullopt;
    }
    if (!url.isSupportScheme()) {
        error = ErrorInfo{slark::Error::SchemeNotSupported};
        return std::nullopt;
    }
    return url;
}

bool validateRequest(const RequestInfo& info, const Url& url) noexcept {
    if (!url.isValid() || info.methodType == HttpMethodType::Unknown) {
        return false;
    }

    for (auto it = info.headers.begin(); it != info.headers.end(); ++it) {
        const auto& [key, value] = *it;
        if (key.empty() || key.find_first_of("\r\n") != std::string::npos ||
            value.find_first_of("\r\n") != std::string::npos) {
            return false;
        }
        if (StringUtil::iequals(key, "Transfer-Encoding")) {
            return false;
        }
        if (StringUtil::iequals(key, "Content-Length")) {
            uint64_t parsedLength = 0;
            const auto [end, error] =
                std::from_chars(value.data(), value.data() + value.size(), parsedLength, 10);
            if (error != std::errc() || end != value.data() + value.size() ||
                parsedLength != info.bodySize()) {
                return false;
            }
        }
        if (StringUtil::iequals(key, "Connection") && !StringUtil::iequals(value, "close") &&
            !StringUtil::iequals(value, "keep-alive")) {
            return false;
        }
        for (auto prior = info.headers.begin(); prior != it; ++prior) {
            if (StringUtil::iequals(prior->first, key)) {
                return false;
            }
        }
    }
    return true;
}

std::string buildRequestString(const RequestInfo& info, const Url& url, bool keepAlive) noexcept {
    if (!validateRequest(info, url)) {
        return {};
    }
    std::string result;
    result.reserve(256);

    bool hasHost = false;
    bool hasContentLength = false;
    bool hasConnection = false;
    for (const auto& [key, value] : info.headers) {
        if (StringUtil::iequals(key, "Host")) {
            hasHost = true;
        } else if (StringUtil::iequals(key, "Content-Length")) {
            hasContentLength = true;
        } else if (StringUtil::iequals(key, "Connection")) {
            hasConnection = true;
        }
        if (hasHost && hasContentLength && hasConnection) {
            break;
        }
    }

    result.append(getMethodName(info.methodType)).append(" ").append(url.path);
    if (!url.query.empty()) {
        result.append("?").append(url.query);
    }
    result.append(" HTTP/1.1\r\n");

    if (!hasHost) {
        result.append("Host: ").append(serializeAuthority(url, false)).append(kCRLF);
    }
    for (const auto& [key, value] : info.headers) {
        if (key.find('\r') != std::string::npos || key.find('\n') != std::string::npos ||
            value.find('\r') != std::string::npos || value.find('\n') != std::string::npos) {
            continue;
        }
        result.append(key).append(": ").append(value).append(kCRLF);
    }

    if (!hasContentLength && !info.bodyEmpty()) {
        result.append("Content-Length: ").append(std::to_string(info.bodySize())).append(kCRLF);
    }

    if (!hasConnection) {
        result.append("Connection: ").append(keepAlive ? "keep-alive" : "close").append(kCRLF);
    }
    result.append(kCRLF);
    return result;
}

}  // namespace slark::http::Util