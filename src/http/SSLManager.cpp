//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//
#if ENABLE_HTTPS

#include "SSLManager.h"

#include <openssl/err.h>
#include <openssl/ssl.h>

#include "Log.hpp"

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

        // Enable certificate chain verification
        SSL_CTX_set_verify(context, SSL_VERIFY_PEER, nullptr);

        // Advertise HTTP/1.1 (extension point reserved for future HTTP/2)
        static constexpr uint8_t kAlpnProtos[] = "\x08http/1.1";
        SSL_CTX_set_alpn_protos(context, kAlpnProtos, sizeof(kAlpnProtos) - 1);

        if (HttpsHelper::configContext) {
            // The platform layer loads the CA bundle (e.g. Android/iOS system certs)
            HttpsHelper::configContext(contextPtr);
        } else {
            // Fallback: load the system default CAs
            SSL_CTX_set_default_verify_paths(context);
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
        // SNI: tell the server the expected domain
        SSL_set_tlsext_host_name(ssl, hostname.c_str());
        // Hostname verification: check the CN/SAN in the certificate after the handshake
        SSL_set1_host(ssl, hostname.c_str());
    }

    res.reset(ssl);
    return res;
}

}  // namespace slark::http

#endif
