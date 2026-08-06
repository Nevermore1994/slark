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
#include <cstring>
#include <optional>

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

struct ConnectAddress final {
    int family = AF_UNSPEC;
    sockaddr_storage storage{};
    socklen_t length = 0;

    [[nodiscard]] bool isValid() const noexcept {
        return (family == AF_INET || family == AF_INET6) && length > 0 &&
               length <= sizeof(storage) && storage.ss_family == family;
    }
};

inline std::optional<ConnectAddress> makeConnectAddress(const addrinfo* info) noexcept {
    if (!info || !info->ai_addr || info->ai_addrlen == 0 ||
        info->ai_addrlen > sizeof(sockaddr_storage) ||
        (info->ai_family != AF_INET && info->ai_family != AF_INET6)) {
        return std::nullopt;
    }

    ConnectAddress address;
    address.family = info->ai_family;
    address.length = info->ai_addrlen;
    std::memcpy(&address.storage, info->ai_addr, info->ai_addrlen);
    return address;
}

using AddressInfoPtr = std::unique_ptr<addrinfo, decltype(&freeaddrinfo)>;

inline AddressInfoPtr makeAddressInfo(addrinfo* info) {
    return AddressInfoPtr(info, &freeaddrinfo);
}

inline void destroyClonedAddressInfo(addrinfo* info) noexcept {
    while (info) {
        auto* next = info->ai_next;
        delete[] reinterpret_cast<Byte*>(info->ai_addr);
        delete[] info->ai_canonname;
        delete info;
        info = next;
    }
}

inline AddressInfoPtr makeClonedAddressInfo(addrinfo* info) {
    return AddressInfoPtr(info, &destroyClonedAddressInfo);
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
