//
// Created by Nevermore on 2024/6/14.
// http-request Url
// Copyright (c) 2024 Nevermore All rights reserved.
//
#pragma once

#include <string>
#include <string_view>

namespace slark::http {

extern const std::string_view kHttpDefaultPort;
extern const std::string_view kHttpsDefaultPort;
extern const std::string_view kHttp;
extern const std::string_view kHttps;

using namespace std::string_view_literals;
struct Url {
    std::string scheme;
    std::string host;
    std::string port = std::string(kHttpDefaultPort);
    std::string path;
    std::string userInfo;
    std::string query;
    std::string fragment;

#ifdef __clang__
#pragma clang diagnostic push
#pragma ide diagnostic ignored "google-explicit-constructor"
#endif
    Url(const std::string& url) {
        parse(url);
    }
#ifdef __clang__
#pragma clang diagnostic pop
#endif //__clang__
    [[nodiscard]] bool isValid() const noexcept {
        return isValid_;
    }

    [[nodiscard]] bool isHttpScheme() const noexcept {
#if ENABLE_HTTPS
        return isHttp() || isHttps();
#else
        return isHttp();
#endif
    };

    [[nodiscard]] bool isHttp() const noexcept {
        return scheme == kHttp;
    }

    [[nodiscard]] bool isHttps() const noexcept {
        return scheme == kHttps;
    }

private:
    void parse(const std::string& url) noexcept;
private:
    bool isValid_ = true;
};

inline void freeUrl(Url* url) noexcept {
    delete url;
}

} //end of namespace slark::http