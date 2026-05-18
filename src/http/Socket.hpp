//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//
#pragma once

#include "Data.h"
#include "Type.h"

#include <cerrno>
#include <chrono>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

namespace slark::http {

using Socket = int;
constexpr Socket kInvalidSocket = kInvalid;

#if defined(__unix__) && !defined(__APPLE__) && !defined(__CYGWIN__)
constexpr int kNoSignal = MSG_NOSIGNAL;
#else
constexpr int kNoSignal = 0;
#endif

constexpr int32_t kDefaultReadSize = 16 * 1024;            // 16KB
constexpr size_t kGrowthThreshold = 256 * 1024;            // 256KB，超过此值后按固定步长增长
constexpr size_t kGrowthStepSize = 128 * 1024;             // 128KB 固定步长
constexpr size_t kMaxReceiveBufferSize = 64 * 1024 * 1024; // 64MB 上限
constexpr int32_t kSocketError = kInvalid;
constexpr int32_t kRetryCode = EINTR;
constexpr int32_t kAgainCode = EAGAIN;
constexpr int32_t kBusyCode = EINPROGRESS;
constexpr int32_t kMaxAgainRetryCount = 16;

inline int32_t getLastError() noexcept {
    return errno;
}

// EINTR retry wrapper
template <typename Func>
std::pair<ssize_t, int32_t> retryOnInterrupt(Func&& func, int32_t maxRetry = kMaxRetryCount) {
    for (int32_t i = 0; i < maxRetry; ++i) {
        ssize_t result = func();
        if (result >= 0) {
            return {result, 0};
        }
        auto err = getLastError();
        if (err != kRetryCode) {
            return {result, err};
        }
    }
    return {-1, kRetryCode};
}

using AddressInfoPtr = std::unique_ptr<addrinfo, decltype(&freeaddrinfo)>;

inline AddressInfoPtr makeAddressInfo(addrinfo* info) {
    return AddressInfoPtr(info, &freeaddrinfo);
}

inline int getAddressFamily(IPVersion ipVersion) noexcept {
    switch (ipVersion) {
        case IPVersion::V4:
            return AF_INET;
        case IPVersion::V6:
            return AF_INET6;
        default:
            return AF_UNSPEC;
    }
}

} // namespace slark::http
