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

        // 跨域重定向时剥离敏感请求头，防止凭证泄露到第三方域名
        Url origUrl(original.url);
        Url newUrl(redirectRequest.url);
        if (origUrl.host != newUrl.host) {
            for (const auto& key : {"Authorization", "Cookie", "Proxy-Authorization"}) {
                redirectRequest.headers.erase(key);
            }
        }

        // RFC 7231：根据重定向状态码处理方法
        switch (redirectCode) {
            case HttpStatusCode::SeeOther:
                // 303：方法必须改为 GET
                redirectRequest.methodType = HttpMethodType::Get;
                redirectRequest.body = nullptr;  // 303 不应携带 body
                break;
            case HttpStatusCode::MovedPermanently:  // 301
            case HttpStatusCode::Found:             // 302
                // 历史原因，很多客户端对 301/302 的 POST 请求也改为 GET
                // 这里保持原方法，由调用方决定是否修改
                break;
            case HttpStatusCode::TemporaryRedirect:  // 307
            case HttpStatusCode::PermanentRedirect:  // 308
                // 必须保持原方法和 body
                break;
            default:
                break;
        }

        return redirectRequest;
    }

    // 兼容旧接口的重载
    [[nodiscard]] static std::optional<RequestInfo> buildRedirectRequest(
        const RequestInfo& original,
        std::string_view location) {
        return buildRedirectRequest(original, HttpStatusCode::Found, location);
    }

 private:
    // 解析重定向URL，处理相对路径；https→http 降级时返回 nullopt
    [[nodiscard]] static std::optional<std::string> resolveRedirectUrl(std::string_view originalUrl,
                                                                       std::string_view location) {
        // 绝对URL（包含scheme）
        if (location.starts_with("http:") || location.starts_with("https:")) {
            // 拒绝 https → http 降级重定向，防止中间人攻击
            Url origUrl(originalUrl);
            if (origUrl.isHttps() && location.starts_with("http:") &&
                !location.starts_with("https:")) {
                return std::nullopt;
            }
            return std::string(location);
        }

        // 解析原始URL获取base
        Url url(originalUrl);
        if (!url.isValid()) {
            return std::string(location);
        }

        std::string result;
        result.reserve(originalUrl.size() + location.size());

        // 协议相对路径（//host/path）
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
        // 相对路径，基于原路径计算
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
