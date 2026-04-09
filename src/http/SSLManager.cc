//
// Created by Nevermore on 2024/6/15.
// slark SSLManager
// Copyright (c) 2024 Nevermore All rights reserved.
//
#if ENABLE_HTTPS

#include "SSLManager.hpp"

#include "Log.hpp"

#include <openssl/err.h>
#include <openssl/ssl.h>

namespace slark::http {

#ifdef __clang__
#pragma clang diagnostic push
#pragma ide diagnostic ignored "modernize-use-equals-default"
#endif

SSLManager::SSLManager() {
#if OPENSSL_VERSION_NUMBER < 0x10100000L
    SSL_library_init();
    OpenSSL_add_all_algorithms();
    SSL_load_error_strings();
#endif
}

SSLManager::~SSLManager() {
#if OPENSSL_VERSION_NUMBER < 0x10100000L
    EVP_cleanup();
    ERR_free_strings();
#endif
}

#ifdef __clang__
#pragma clang diagnostic pop
#endif

std::function<void(SSLContextPtr&)> HttpsHelper::configContext = nullptr;

SSLContextPtr& SSLManager::shareContext() {
    static std::once_flag flag;
    static SSLManager instance;
    static SSLContextPtr contextPtr(nullptr, SSL_CTX_free);

    std::call_once(flag, [] {
        auto* context = SSL_CTX_new(TLS_client_method());
        if (!context) {
            throw std::runtime_error("Failed to create SSL context");
        }
        contextPtr.reset(context);
        if (HttpsHelper::configContext) {
            HttpsHelper::configContext(contextPtr);
        }
    });

    return contextPtr;
}

SSLPtr SSLManager::create(Socket socket, const std::string& hostname) noexcept {
    SSLPtr res(nullptr, SSL_free);
    auto* ssl = SSL_new(shareContext().get());
    if (!ssl) {
        return res;
    }

    SSL_set_fd(ssl, socket);
    if (!hostname.empty()) {
        SSL_set_tlsext_host_name(ssl, hostname.c_str());
    }

    res.reset(ssl);
    return res;
}

} // namespace slark::http

#endif
