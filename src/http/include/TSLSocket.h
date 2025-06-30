//
// Created by Nevermore on 2024/6/17.
// slark TSLSocket
// Copyright (c) 2024 Nevermore All rights reserved.
//

#pragma once

#if ENABLE_HTTPS
#include "Socket.h"
#include "SSLManager.h"

namespace slark::http {

class TSLSocket final : public ISocket {
public:
    explicit TSLSocket(IPVersion ipVersion = IPVersion::V4);

    ~TSLSocket() override;

    SocketResult connect(const AddressInfoPtr& address, int64_t timeout) noexcept override;

    [[nodiscard]] std::tuple<SocketResult, int64_t> send(const std::string_view& data) const noexcept override;

    [[nodiscard]] std::tuple<SocketResult, DataPtr> receive() const noexcept override;

    [[nodiscard]] SocketResult canSend(int64_t timeout) const noexcept override;

    [[nodiscard]] SocketResult canReceive(int64_t timeout) const noexcept override;

    void close() noexcept override;
private:
    SSLPtr sslPtr;
};

} //end of namespace slark::http

#endif //end if ENABLE_HTTPS
