//
// Created by Nevermore on 2024/6/14.
// slark Url
// Copyright (c) 2024 Nevermore All rights reserved.
//
#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace slark::http {

extern const std::string_view kHttpDefaultPort;
extern const std::string_view kHttpsDefaultPort;
extern const std::string_view kHttp;
extern const std::string_view kHttps;

using namespace std::string_view_literals;

enum class UrlError : uint8_t {
    None = 0,
    EmptyUrl,
    MissingScheme,
    InvalidScheme,
    EmptyAuthority,
    InvalidHost,
    InvalidPort,
    PortOutOfRange,
    InvalidIPv6,
};

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
    Url(std::string_view url) {
        parse(url);
    }
#ifdef __clang__
#pragma clang diagnostic pop
#endif

    [[nodiscard]] bool isValid() const noexcept {
        return error_ == UrlError::None;
    }

    [[nodiscard]] UrlError error() const noexcept {
        return error_;
    }

    [[nodiscard]] bool isSupportScheme() const noexcept {
#if ENABLE_HTTPS
        return isHttp() || isHttps();
#else
        return isHttp();
#endif
    }

    [[nodiscard]] bool isHttp() const noexcept {
        return scheme == kHttp;
    }

    [[nodiscard]] bool isHttps() const noexcept {
        return scheme == kHttps;
    }

    [[nodiscard]] std::string_view url() const noexcept {
        return rawUrl_;
    }

    [[nodiscard]] uint16_t portNumber() const noexcept {
        try {
            return static_cast<uint16_t>(std::stoi(std::string(port)));
        } catch (...) {
            return isHttps() ? 443 : 80;
        }
    }

private:
    void parse(std::string_view url) noexcept;

    UrlError error_ = UrlError::None;
    std::string rawUrl_;
};

inline void freeUrl(Url* url) noexcept {
    delete url;
}

} // namespace slark::http
