//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//
#include "Url.h"

#include <algorithm>

namespace slark::http {

using namespace std::string_view_literals;

const std::string_view kHttpDefaultPort = "80"sv;
const std::string_view kHttpsDefaultPort = "443"sv;
const std::string_view kHttp = "http"sv;
const std::string_view kHttps = "https"sv;

bool isValidScheme(std::string_view scheme) noexcept {
    if (scheme.empty()) {
        return false;
    }
    return std::all_of(scheme.begin(), scheme.end(), [](unsigned char c) {
        return c == '-' || std::isalnum(c);
    });
}

void Url::parse(std::string_view url) noexcept {
    rawUrl_ = url;

    if (url.empty()) {
        error_ = UrlError::EmptyUrl;
        return;
    }

    constexpr auto kSchemeFlag = "://"sv;
    auto flagPos = url.find(kSchemeFlag);

    if (flagPos == std::string_view::npos) {
        error_ = UrlError::MissingScheme;
        return;
    }

    auto schemeView = url.substr(0, flagPos);
    if (!isValidScheme(schemeView)) {
        error_ = UrlError::InvalidScheme;
        return;
    }

    scheme = schemeView;
    std::transform(scheme.begin(), scheme.end(), scheme.begin(), [](unsigned char c) {
        return std::tolower(c);
    });
    auto view = url.substr(flagPos + kSchemeFlag.size());

    if (view.empty()) {
        error_ = UrlError::EmptyAuthority;
        return;
    }

    // Extract fragment
    if (auto fragmentPos = view.find('#'); fragmentPos != std::string_view::npos) {
        fragment = view.substr(fragmentPos + 1);
        view = view.substr(0, fragmentPos);
    }

    // Extract query
    if (auto queryPos = view.find('?'); queryPos != std::string_view::npos) {
        query = view.substr(queryPos + 1);
        view = view.substr(0, queryPos);
    }

    // Extract path
    if (auto pathPos = view.find('/'); pathPos != std::string_view::npos) {
        path = view.substr(pathPos);
        view = view.substr(0, pathPos);
    } else {
        path = "/";
    }

    // Extract user info
    if (auto userInfoPos = view.find('@'); userInfoPos != std::string_view::npos) {
        userInfo = view.substr(0, userInfoPos);
        view = view.substr(userInfoPos + 1);
    }

    // IPv6 地址处理
    bool isIPv6 = !view.empty() && view[0] == '[';
    if (isIPv6) {
        auto closeBracket = view.find(']');
        if (closeBracket == std::string_view::npos) {
            error_ = UrlError::InvalidIPv6;
            return;
        }
        host = view.substr(1, closeBracket - 1);
        auto afterBracket = view.substr(closeBracket + 1);
        if (!afterBracket.empty() && afterBracket[0] == ':') {
            port = afterBracket.substr(1);
        } else if (afterBracket.empty()) {
            port = isHttps() ? kHttpsDefaultPort : kHttpDefaultPort;
        } else {
            error_ = UrlError::InvalidIPv6;
            return;
        }
    } else {
        auto portPos = view.rfind(':');
        if (portPos != std::string_view::npos) {
            port = view.substr(portPos + 1);
            view = view.substr(0, portPos);
        } else {
            port = isHttps() ? kHttpsDefaultPort : kHttpDefaultPort;
        }
        host = view;
    }

    if (host.empty()) {
        error_ = UrlError::InvalidHost;
        return;
    }

    // port 数值范围校验
    if (!port.empty()) {
        for (char c : port) {
            if (!std::isdigit(static_cast<unsigned char>(c))) {
                error_ = UrlError::InvalidPort;
                return;
            }
        }
        auto portNumResult = StringUtil::fromChars<int32_t>(port);
        if (!portNumResult || *portNumResult < 0 || *portNumResult > 65535) {
            error_ = UrlError::PortOutOfRange;
            return;
        }
    }

    error_ = UrlError::None;
}

} // namespace slark::http
