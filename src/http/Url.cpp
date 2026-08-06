//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//
#include "Url.h"

#include <algorithm>
#include <cctype>

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

namespace {

bool containsInvalidUrlCharacter(std::string_view value) noexcept {
    return std::any_of(value.begin(), value.end(), [](unsigned char c) {
        return c <= 0x20 || c == 0x7f;
    });
}

std::string_view extractComponents(std::string_view view,
                                   std::string& fragment,
                                   std::string& query,
                                   std::string& path,
                                   std::string& userInfo) noexcept {
    if (auto fragmentPos = view.find('#'); fragmentPos != std::string_view::npos) {
        fragment = view.substr(fragmentPos + 1);
        view = view.substr(0, fragmentPos);
    }
    if (auto queryPos = view.find('?'); queryPos != std::string_view::npos) {
        query = view.substr(queryPos + 1);
        view = view.substr(0, queryPos);
    }
    if (auto pathPos = view.find('/'); pathPos != std::string_view::npos) {
        path = view.substr(pathPos);
        view = view.substr(0, pathPos);
    } else {
        path = "/";
    }
    if (auto userInfoPos = view.find('@'); userInfoPos != std::string_view::npos) {
        userInfo = view.substr(0, userInfoPos);
        view = view.substr(userInfoPos + 1);
    }
    return view;
}

bool parseHostPort(std::string_view view,
                   bool https,
                   std::string& host,
                   std::string& port,
                   UrlError& error) noexcept {
    if (!view.empty() && view[0] == '[') {
        auto closeBracket = view.find(']');
        if (closeBracket == std::string_view::npos) {
            error = UrlError::InvalidIPv6;
            return false;
        }
        host = view.substr(1, closeBracket - 1);
        auto afterBracket = view.substr(closeBracket + 1);
        if (!afterBracket.empty() && afterBracket[0] == ':') {
            port = afterBracket.substr(1);
        } else if (afterBracket.empty()) {
            port = https ? kHttpsDefaultPort : kHttpDefaultPort;
        } else {
            error = UrlError::InvalidIPv6;
            return false;
        }
    } else {
        auto portPos = view.rfind(':');
        if (portPos != std::string_view::npos) {
            port = view.substr(portPos + 1);
            view = view.substr(0, portPos);
        } else {
            port = https ? kHttpsDefaultPort : kHttpDefaultPort;
        }
        host = view;
    }
    if (host.empty()) {
        error = UrlError::InvalidHost;
        return false;
    }
    return true;
}

bool validatePort(std::string_view port, UrlError& error) noexcept {
    if (port.empty()) {
        return true;
    }
    for (char c : port) {
        if (!std::isdigit(static_cast<unsigned char>(c))) {
            error = UrlError::InvalidPort;
            return false;
        }
    }
    auto portNumResult = StringUtil::fromChars<int32_t>(port);
    if (!portNumResult || *portNumResult < 0 || *portNumResult > 65535) {
        error = UrlError::PortOutOfRange;
        return false;
    }
    return true;
}

}  // namespace

void Url::parse(std::string_view url) noexcept {
    rawUrl_ = url;

    if (url.empty()) {
        error_ = UrlError::EmptyUrl;
        return;
    }
    if (containsInvalidUrlCharacter(url)) {
        error_ = UrlError::InvalidCharacter;
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

    view = extractComponents(view, fragment, query, path, userInfo);
    if (!parseHostPort(view, isHttps(), host, port, error_)) {
        return;
    }
    if (!validatePort(port, error_)) {
        return;
    }
    error_ = UrlError::None;
}

std::string serializeAuthority(const Url& url, bool includeUserInfo) {
    std::string result;
    result.reserve(url.host.size() + url.userInfo.size() + url.port.size() + 4);

    if (includeUserInfo && !url.userInfo.empty()) {
        result.append(url.userInfo).append("@");
    }

    if (url.host.find(':') != std::string::npos) {
        result.append("[").append(url.host).append("]");
    } else {
        result.append(url.host);
    }

    const bool isDefaultPort = (url.isHttp() && url.port == kHttpDefaultPort) ||
                               (url.isHttps() && url.port == kHttpsDefaultPort);
    if (!isDefaultPort && !url.port.empty()) {
        result.append(":").append(url.port);
    }
    return result;
}

}  // namespace slark::http
