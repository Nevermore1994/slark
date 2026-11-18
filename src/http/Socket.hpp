//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//
#pragma once

#include <arpa/inet.h>
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
#include "Error.h"
#include "Type.h"

namespace slark::http {

using Socket = int;
constexpr Socket kInvalidSocket = kInvalid;

[[nodiscard]] inline std::optional<Error> mapCommonSocketErrno(int error) noexcept {
    switch (error) {
        case ECONNRESET:
            return Error::ConnectionReset;
        case EBADF:
        case ENOTSOCK:
            return Error::InvalidSocket;
        case EAGAIN:
            return Error::WouldBlock;
#if EWOULDBLOCK != EAGAIN
        case EWOULDBLOCK:
            return Error::WouldBlock;
#endif
        default:
            return std::nullopt;
    }
}

#if defined(__unix__) && !defined(__APPLE__) && !defined(__CYGWIN__)
constexpr int kNoSignal = MSG_NOSIGNAL;
#else
constexpr int kNoSignal = 0;
#endif

// An idle keep-alive connection must have nothing to read: EOF means the peer closed it, and
// pending bytes (an error, a TLS alert or a late session ticket) mean it cannot be trusted.
inline bool isIdleConnectionUsable(Socket fd) noexcept {
    if (fd == kInvalidSocket) {
        return false;
    }
    char probe = 0;
    ssize_t received = 0;
    do {
        received = ::recv(fd, &probe, 1, MSG_PEEK | MSG_DONTWAIT);
    } while (received < 0 && errno == EINTR);
    return received < 0 && (errno == EAGAIN || errno == EWOULDBLOCK);
}

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

// True for IPv4/IPv6 literals. SNI forbids carrying an IP literal; TLS peer
// matching for literal hosts must use the certificate's IP SAN instead.
inline bool isIpLiteral(const std::string& host) noexcept {
    if (host.empty()) {
        return false;
    }
    in_addr v4{};
    if (inet_pton(AF_INET, host.c_str(), &v4) == 1) {
        return true;
    }
    in6_addr v6{};
    return inet_pton(AF_INET6, host.c_str(), &v6) == 1;
}

using AddressInfoPtr = std::shared_ptr<addrinfo>;

inline AddressInfoPtr makeAddressInfo(addrinfo* info) {
    return info ? AddressInfoPtr(info, &freeaddrinfo) : AddressInfoPtr();
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
    return info ? AddressInfoPtr(info, &destroyClonedAddressInfo) : AddressInfoPtr();
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
