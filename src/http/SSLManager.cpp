//
// Created by Nevermore on 2024/6/15.
// slark SSLManager
// Copyright (c) 2024 Nevermore All rights reserved.
//
#if ENABLE_HTTPS
#include "SSLManager.h"
#include "Socket.h"
#include "Type.h"
#include <thread>


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
    static SSLManager instance; //Initialize OpenSSL library
    static SSLContextPtr contextPtr(nullptr, SSL_CTX_free);
    std::call_once(flag, [] {
        auto sslMethod = TLS_client_method();
        SSL_CTX* context = SSL_CTX_new(sslMethod);
        if (context) {
            contextPtr.reset(context);
            if (HttpsHelper::configContext) {
                HttpsHelper::configContext(contextPtr);
            }
        } else {
            throw std::runtime_error("create SSL Context error.");
        }
    });
    return contextPtr;
}

SSLPtr SSLManager::create(Socket socket) noexcept {
    SSLPtr res(nullptr, SSL_free);
    SSL* ssl = SSL_new(SSLManager::shareContext().get());
    if (!ssl) {
        return res;
    }
    SSL_set_fd(ssl, socket);
    res.reset(ssl);
    return res;
}

SocketResult SSLManager::connect(SSLPtr& sslPtr) noexcept {
    SocketResult res;
    auto result = SSL_connect(sslPtr.get());
    if (result == 0) {
        res.resultCode = ResultCode::Disconnected;
    } else if (result < 0) {
        res.resultCode = ResultCode::Failed;
        res.errorCode = SSL_get_error(sslPtr.get(), 0);
        ERR_print_errors_fp(stdout);
    }
    return res;
}

std::tuple<SocketResult, int64_t> SSLManager::write(const SSLPtr& sslPtr, const std::string_view& data) noexcept {
    using namespace std::chrono_literals;
    SocketResult res;
    if (sslPtr == nullptr) {
        res.resultCode = ResultCode::Failed;
        return {res, 0};
    }
    int64_t sendLength = 0;
    bool isNeedRetry = false;
    int32_t retryCount = 0;
    do {
        retryCount++;
        sendLength = SSL_write(sslPtr.get(), data.data(), static_cast<int>(data.size()));
        if (sendLength == 0) {
            res.resultCode = ResultCode::Disconnected;
            break;
        } else if (sendLength < 0) {
            res.resultCode = ResultCode::Failed;
            res.errorCode = SSL_get_error(sslPtr.get(), 0);
            if (retryCount >= kMaxRetryCount) {
                res.resultCode = ResultCode::RetryReachMaxCount;
                break;
            }
            if (SSL_ERROR_WANT_WRITE == res.errorCode || SSL_ERROR_WANT_READ == res.errorCode) {
                isNeedRetry = true;
            }
            std::this_thread::sleep_for(1ms);
        } else {
            res.reset();
            break;
        }
    } while (isNeedRetry);
    return {res, sendLength};
}

std::tuple<SocketResult, DataPtr> SSLManager::read(const SSLPtr& sslPtr) noexcept {
    using namespace std::chrono_literals;
    SocketResult res;
    if (sslPtr == nullptr) {
        res.resultCode = ResultCode::Failed;
        return {res, nullptr};
    }
    bool isNeedRetry = false;
    auto data = std::make_unique<Data>(kDefaultReadSize);
    int32_t retryCount = 0;
    do {
        retryCount++;
        auto recvLength = static_cast<uint64_t>(SSL_read(sslPtr.get(), data->rawData, kDefaultReadSize));
        if (recvLength == 0) {
            res.resultCode = ResultCode::Disconnected;
            break;
        } else if (recvLength < 0) {
            res.resultCode = ResultCode::Failed;
            res.errorCode = SSL_get_error(sslPtr.get(), 0);
            if (retryCount >= kMaxRetryCount) {
                res.resultCode = ResultCode::RetryReachMaxCount;
                break;
            }
            if ((SSL_ERROR_WANT_WRITE == res.errorCode || SSL_ERROR_WANT_READ == res.errorCode)) {
                isNeedRetry = true;
            }
            std::this_thread::sleep_for(1ms);
        } else {
            res.reset();
            data->length = recvLength;
            break;
        }
    } while (isNeedRetry);
    return {res, std::move(data)};
}

void SSLManager::close(SSLPtr& sslPtr) noexcept {
    if (sslPtr) {
        SSL_shutdown(sslPtr.get());
    }
}

} //end of namespace slark::http

#endif //endif ENABLE_HTTPS
