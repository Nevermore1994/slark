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

inline bool isPublicIpv4(uint32_t value) noexcept {
    return (value & 0xff000000u) != 0x00000000u &&
           (value & 0xff000000u) != 0x7f000000u &&
           (value & 0xff000000u) != 0x0a000000u &&
           (value & 0xffc00000u) != 0x64400000u &&
           (value & 0xffff0000u) != 0xa9fe0000u &&
           (value & 0xfff00000u) != 0xac100000u &&
           (value & 0xffffff00u) != 0xc0000000u &&
           (value & 0xffffff00u) != 0xc0000200u &&
           (value & 0xffff0000u) != 0xc0a80000u &&
           (value & 0xfffe0000u) != 0xc6120000u &&
           (value & 0xffffff00u) != 0xc6336400u &&
           (value & 0xffffff00u) != 0xcb007100u &&
           (value & 0xf0000000u) != 0xe0000000u &&
           (value & 0xf0000000u) != 0xf0000000u;
}

inline bool isPublicConnectAddress(const ConnectAddress& address) noexcept {
    if (!address.isValid()) {
        return false;
    }

    if (address.family == AF_INET) {
        uint32_t value = 0;
        std::memcpy(&value, &reinterpret_cast<const sockaddr_in&>(address.storage).sin_addr,
                    sizeof(value));
        return isPublicIpv4(ntohl(value));
    }

    const auto& address6 = reinterpret_cast<const sockaddr_in6&>(address.storage).sin6_addr;
    if (IN6_IS_ADDR_V4MAPPED(&address6)) {
        uint32_t mapped = 0;
        std::memcpy(&mapped, &address6.s6_addr[12], sizeof(mapped));
        return isPublicIpv4(ntohl(mapped));
    }
    return !IN6_IS_ADDR_UNSPECIFIED(&address6) && !IN6_IS_ADDR_LOOPBACK(&address6) &&
           !IN6_IS_ADDR_LINKLOCAL(&address6) && !IN6_IS_ADDR_SITELOCAL(&address6) &&
           !IN6_IS_ADDR_MULTICAST(&address6) &&
           std::memcmp(address6.s6_addr, "\x20\x01\x0d\xb8", 4) != 0 &&
           (address6.s6_addr[0] & 0xfeu) != 0xfcu;
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
