//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//
#pragma once

#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include <cerrno>
#include <chrono>

#include "Data.h"
#include "Type.h"

namespace slark::http {

using Socket = int;
constexpr Socket kInvalidSocket = kInvalid;

#if defined(__unix__) && !defined(__APPLE__) && !defined(__CYGWIN__)
constexpr int kNoSignal = MSG_NOSIGNAL;
#else
constexpr int kNoSignal = 0;
#endif

constexpr int32_t kDefaultReadSize = 16 * 1024;

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

}  // namespace slark::http
