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
            if (StringUtil::iequals(k, "Location") && !v.empty()) {
                return v;
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

        // Strip sensitive headers on cross-domain redirects to prevent credential leaks
        // to third-party domains
        Url origUrl(original.url);
        Url newUrl(redirectRequest.url);
        if (origUrl.host != newUrl.host) {
            for (const auto& key : {"Authorization", "Cookie", "Proxy-Authorization"}) {
                redirectRequest.headers.erase(key);
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
    // Resolve a redirect URL, handling relative paths; returns nullopt on https→http downgrade
    [[nodiscard]] static std::optional<std::string> resolveRedirectUrl(std::string_view originalUrl,
                                                                       std::string_view location) {
        // Absolute URL (includes scheme)
        if (location.starts_with("http:") || location.starts_with("https:")) {
            // Reject https → http downgrade redirects to prevent MITM attacks
            Url origUrl(originalUrl);
            if (origUrl.isHttps() && location.starts_with("http:") &&
                !location.starts_with("https:")) {
                return std::nullopt;
            }
            return std::string(location);
        }

        // Parse the original URL to obtain the base
        Url url(originalUrl);
        if (!url.isValid()) {
            return std::string(location);
        }

        std::string result;
        result.reserve(originalUrl.size() + location.size());

        // Protocol-relative path (//host/path)
        if (location.starts_with("//")) {
            result.append(url.scheme);
            result.append(":");
            result.append(location);
            return result;
        }

        appendAuthority(result, url);
        appendRelativePath(result, url, location);
        return result;
    }

    static void appendAuthority(std::string& result, const Url& url) {
        result.append(url.scheme);
        result.append("://");
        if (!url.userInfo.empty()) {
            result.append(url.userInfo);
            result.append("@");
        }
        result.append(url.host);
        if ((url.isHttp() && url.port != "80") || (url.isHttps() && url.port != "443")) {
            result.append(":");
            result.append(url.port);
        }
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
