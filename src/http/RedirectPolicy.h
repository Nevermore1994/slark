//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#pragma once

#include <StringUtil.h>

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
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
        redirectRequest.isRedirectTarget = true;

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

    struct ReferenceParts final {
        std::string_view path;
        std::string_view query;
        std::string_view fragment;
        bool hasQuery = false;
        bool hasFragment = false;
    };

    [[nodiscard]] static ReferenceParts splitReference(std::string_view reference) noexcept {
        ReferenceParts parts;
        const auto fragmentPos = reference.find('#');
        if (fragmentPos != std::string_view::npos) {
            parts.hasFragment = true;
            parts.fragment = reference.substr(fragmentPos + 1);
            reference = reference.substr(0, fragmentPos);
        }

        const auto queryPos = reference.find('?');
        if (queryPos != std::string_view::npos) {
            parts.hasQuery = true;
            parts.query = reference.substr(queryPos + 1);
            parts.path = reference.substr(0, queryPos);
        } else {
            parts.path = reference;
        }
        return parts;
    }

    static void removeLastPathSegment(std::string& output) {
        const auto slash = output.rfind('/');
        if (slash == std::string::npos) {
            output.clear();
        } else {
            output.erase(slash);
        }
    }

    [[nodiscard]] static std::string removeDotSegments(std::string_view input) {
        std::string output;
        output.reserve(input.size());
        while (!input.empty()) {
            if (input.starts_with("../")) {
                input.remove_prefix(3);
            } else if (input.starts_with("./")) {
                input.remove_prefix(2);
            } else if (input == "/.") {
                input = "/";
            } else if (input.starts_with("/./")) {
                input.remove_prefix(2);
            } else if (input == "/..") {
                input = "/";
                removeLastPathSegment(output);
            } else if (input.starts_with("/../")) {
                input.remove_prefix(3);
                removeLastPathSegment(output);
            } else if (input == "." || input == "..") {
                input = {};
            } else {
                auto segmentEnd = input.find('/', input.front() == '/' ? 1 : 0);
                if (segmentEnd == std::string_view::npos) {
                    output.append(input);
                    input = {};
                } else {
                    output.append(input.substr(0, segmentEnd));
                    input.remove_prefix(segmentEnd);
                }
            }
        }
        return output;
    }

    [[nodiscard]] static std::string mergePaths(const Url& url, std::string_view path) {
        std::string_view basePath = url.path;
        if (basePath.empty()) {
            basePath = "/";
        }
        const auto lastSlash = basePath.rfind('/');
        if (lastSlash == std::string_view::npos) {
            return "/" + std::string(path);
        }
        return std::string(basePath.substr(0, lastSlash + 1)) + std::string(path);
    }

    // Resolve a redirect URL according to RFC 3986 section 5.2.
    [[nodiscard]] static std::optional<std::string> resolveRedirectUrl(std::string_view originalUrl,
                                                                       std::string_view location) {
        auto locationValue = trimOws(location);
        if (locationValue.find_first_of("\r\n") != std::string::npos) {
            return std::nullopt;
        }

        Url url(originalUrl);
        if (!url.isValid() || !url.isSupportScheme()) {
            return std::nullopt;
        }

        if (locationValue.starts_with("//")) {
            return validateResolvedUrl(url, url.scheme + ":" + locationValue);
        }

        const auto firstPathDelimiter = locationValue.find_first_of("/?#");
        const auto schemeDelimiter = locationValue.find(':');
        if (schemeDelimiter != std::string_view::npos &&
            (firstPathDelimiter == std::string_view::npos || schemeDelimiter < firstPathDelimiter)) {
            return validateResolvedUrl(url, locationValue);
        }

        const auto reference = splitReference(locationValue);
        const auto basePath = url.path.empty() ? "/" : url.path;
        std::string targetPath;
        std::string targetQuery;
        bool hasQuery = reference.hasQuery;

        if (reference.path.empty()) {
            targetPath = basePath;
            if (reference.hasQuery) {
                targetQuery = reference.query;
            } else {
                targetQuery = url.query;
                hasQuery = !url.query.empty();
            }
        } else if (reference.path.starts_with('/')) {
            targetPath = removeDotSegments(reference.path);
        } else {
            targetPath = removeDotSegments(mergePaths(url, reference.path));
        }
        if (reference.hasQuery) {
            targetQuery = reference.query;
        }

        std::string result;
        result.reserve(originalUrl.size() + locationValue.size() + 4);
        appendAuthority(result, url);
        result.append(targetPath.empty() ? "/" : targetPath);
        if (hasQuery) {
            result.append("?").append(targetQuery);
        }
        if (reference.hasFragment) {
            result.append("#").append(reference.fragment);
        }
        return validateResolvedUrl(url, std::move(result));
    }

    static void appendAuthority(std::string& result, const Url& url) {
        result.append(url.scheme);
        result.append("://");
        result.append(serializeAuthority(url));
    }
};

}  // namespace slark::http
