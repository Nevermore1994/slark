//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#pragma once

#include <StringUtil.h>

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>

#include "Type.h"
#include "Url.h"

namespace slark::http {

class RedirectPolicy {
 public:
    static constexpr int32_t kMaxRedirectCount = 10;

    [[nodiscard]] bool shouldRedirect(const HttpResponse& response,
                                      const RequestInfo& request,
                                      int32_t redirectCount) const noexcept {
        return response.isNeedRedirect() && request.isAllowRedirect &&
               redirectCount < kMaxRedirectCount;
    }

    [[nodiscard]] std::optional<std::string> extractRedirectLocation(
        const std::unordered_map<std::string, std::string>& headers) const {
        for (const auto& [k, v] : headers) {
            if (!StringUtil::iequals(k, "Location")) {
                continue;
            }
            auto location = trimOws(v);
            if (!location.empty() && location.find_first_of("\r\n") == std::string::npos) {
                return location;
            }
        }
        return std::nullopt;
    }

    [[nodiscard]] static std::optional<RequestInfo> buildRedirectRequest(
        const RequestInfo& original,
        HttpStatusCode redirectCode,
        std::string_view location) {
        auto resolvedUrl = resolveRedirectUrl(original.url, location);
        if (!resolvedUrl.has_value()) {
            return std::nullopt;
        }
        RequestInfo redirectRequest = original;
        redirectRequest.url = std::move(*resolvedUrl);

        Url origUrl(original.url);
        Url newUrl(redirectRequest.url);
        if (!sameOrigin(origUrl, newUrl)) {
            for (auto it = redirectRequest.headers.begin(); it != redirectRequest.headers.end();) {
                if (isSensitiveHeader(it->first)) {
                    it = redirectRequest.headers.erase(it);
                } else {
                    ++it;
                }
            }
        }

        // RFC 7231: method handling by redirect status code
        switch (redirectCode) {
            case HttpStatusCode::SeeOther:
                // 303: method must be changed to GET
                redirectRequest.methodType = HttpMethodType::Get;
                redirectRequest.body = nullptr;  // 303 must not carry a body
                break;
            case HttpStatusCode::MovedPermanently:  // 301
            case HttpStatusCode::Found:             // 302
                // Historically many clients rewrite POST to GET for 301/302.
                // Keep the original method here; the caller decides whether to change it.
                break;
            case HttpStatusCode::TemporaryRedirect:  // 307
            case HttpStatusCode::PermanentRedirect:  // 308
                // Original method and body must be preserved
                break;
            default:
                break;
        }

        return redirectRequest;
    }

    // Overload for backward compatibility
    [[nodiscard]] static std::optional<RequestInfo> buildRedirectRequest(
        const RequestInfo& original,
        std::string_view location) {
        return buildRedirectRequest(original, HttpStatusCode::Found, location);
    }

 private:
    [[nodiscard]] static std::string trimOws(std::string_view value) {
        while (!value.empty() && (value.front() == ' ' || value.front() == '\t')) {
            value.remove_prefix(1);
        }
        while (!value.empty() && (value.back() == ' ' || value.back() == '\t')) {
            value.remove_suffix(1);
        }
        return std::string(value);
    }

    [[nodiscard]] static bool isSensitiveHeader(std::string_view name) noexcept {
        return StringUtil::iequals(name, "Authorization") ||
               StringUtil::iequals(name, "Cookie") ||
               StringUtil::iequals(name, "Proxy-Authorization");
    }

    [[nodiscard]] static bool sameOrigin(const Url& lhs, const Url& rhs) noexcept {
        return lhs.isValid() && rhs.isValid() && StringUtil::iequals(lhs.scheme, rhs.scheme) &&
               StringUtil::iequals(lhs.host, rhs.host) && lhs.portNumber() == rhs.portNumber();
    }

    [[nodiscard]] static std::optional<std::string> validateResolvedUrl(
        const Url& original,
        std::string resolved) {
        Url target(resolved);
        if (!target.isValid() || !target.isSupportScheme()) {
            return std::nullopt;
        }
        if (original.isHttps() && target.isHttp()) {
            return std::nullopt;
        }
        return resolved;
    }

    // Resolve a redirect URL, handling relative paths and rejecting unsafe schemes/downgrades.
    [[nodiscard]] static std::optional<std::string> resolveRedirectUrl(std::string_view originalUrl,
                                                                       std::string_view location) {
        auto locationValue = trimOws(location);
        if (locationValue.empty() || locationValue.find_first_of("\r\n") != std::string::npos) {
            return std::nullopt;
        }

        Url url(originalUrl);
        if (!url.isValid() || !url.isSupportScheme()) {
            return std::nullopt;
        }

        if (locationValue.starts_with("//")) {
            return validateResolvedUrl(url, url.scheme + ":" + locationValue);
        }

        if (locationValue.find("://") != std::string::npos) {
            return validateResolvedUrl(url, locationValue);
        }

        const auto firstPathDelimiter = locationValue.find_first_of("/?#");
        const auto schemeDelimiter = locationValue.find(':');
        if (schemeDelimiter != std::string::npos &&
            (firstPathDelimiter == std::string::npos || schemeDelimiter < firstPathDelimiter)) {
            return std::nullopt;
        }

        std::string result;
        result.reserve(originalUrl.size() + locationValue.size());
        appendAuthority(result, url);
        appendRelativePath(result, url, locationValue);
        return validateResolvedUrl(url, std::move(result));
    }

    static void appendAuthority(std::string& result, const Url& url) {
        result.append(url.scheme);
        result.append("://");
        result.append(serializeAuthority(url));
    }

    static void appendRelativePath(std::string& result, const Url& url, std::string_view location) {
        if (location.starts_with('?')) {
            result.append(url.path.empty() ? "/" : url.path);
            result.append(location);
            return;
        }
        if (location.starts_with('#')) {
            result.append(url.path.empty() ? "/" : url.path);
            if (!url.query.empty()) {
                result.append("?");
                result.append(url.query);
            }
            result.append(location);
            return;
        }
        if (location.starts_with('/')) {
            result.append(location);
            return;
        }
        // Relative path, computed from the original path
        std::string basePath = url.path.empty() ? "/" : url.path;
        auto lastSlash = basePath.rfind('/');
        if (lastSlash != std::string::npos) {
            result.append(basePath.substr(0, lastSlash + 1));
        } else {
            result.append("/");
        }
        result.append(location);
    }
};

}  // namespace slark::http
