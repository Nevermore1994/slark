//
// Created by Nevermore on 2024/6/14.
// slark Url
// Copyright (c) 2024 Nevermore All rights reserved.
//
#include "Url.h"
#include <algorithm>

namespace slark::http {

using namespace std::string_view_literals;

const std::string_view kHttpDefaultPort = "80"sv;
[[maybe_unused]] const std::string_view kHttpsDefaultPort = "443"sv;
const std::string_view kHttp = "http"sv;
const std::string_view kHttps = "https"sv;

bool checkScheme(const std::string_view& scheme) {
    if (scheme.empty()) {
        return false;
    }
    return std::all_of(scheme.begin(), scheme.end(), [](const auto& c){
        return c == '-' || static_cast<bool>(std::isalnum(static_cast<unsigned char>(c)));
    });
}

void Url::parse(std::string_view url) noexcept {
    auto urlView = url;
    auto kUrlFlag = "://"sv;
    auto flagPos = urlView.find(kUrlFlag);
    if (flagPos == std::string_view::npos) {
        isValid_ = false;
        return;
    }
    auto schemeView = urlView.substr(0, flagPos);
    if (checkScheme(schemeView)) {
        scheme = schemeView;
    } else {
        isValid_ = false;
        return;
    }

    auto view = urlView.substr(flagPos + kUrlFlag.size());
    auto fragmentPos = view.find('#');
    if (fragmentPos != std::string_view::npos) {
        fragment = view.substr(fragmentPos + 1);
        view = view.substr(0, fragmentPos);
    }

    auto queryPos = view.find('?');
    if (queryPos != std::string_view::npos) {
        query = view.substr(queryPos + 1);
        view = view.substr(0, queryPos);
    }

    auto pathPos = view.find('/');
    if (pathPos != std::string_view::npos) {
        path = view.substr(pathPos);
        view = view.substr(0, pathPos);
    } else {
        path = "/";
    }

    auto userInfoPos = view.find('@');
    if (userInfoPos != std::string_view::npos) {
        userInfo = view.substr(0, userInfoPos);
        view = view.substr(userInfoPos + 1);
    }

    auto portPos = view.rfind(':');
    if (portPos != std::string_view::npos && (view[0] != '[' || view.find(']') < portPos)) {
        port = view.substr(portPos + 1);
        view = view.substr(0, portPos);
    } else if (isHttp()) {
        port = kHttpDefaultPort;
    } else if (isHttps()) {
        port = kHttpsDefaultPort;
    }
    host = view;
    isValid_ = true;
}

} //end of namespace slark::http
