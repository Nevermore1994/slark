//
// Created by Nevermore on 2024/6/10.
// slark socket
// Copyright (c) 2024 Nevermore All rights reserved.
//
#pragma once

#include "Socket.h"

namespace slark::http {

class PlainSocket final : public ISocket {
public:
    explicit PlainSocket(IPVersion ipVersion = IPVersion::V4);
    ~PlainSocket() override;

    PlainSocket(const PlainSocket&) = delete;
    PlainSocket& operator=(const PlainSocket&) = delete;
    PlainSocket(PlainSocket&&) noexcept = default;
    PlainSocket& operator=(PlainSocket&&) noexcept = default;

    SocketResult connect(const AddressInfoPtr& address, int64_t timeout) noexcept override;

    [[nodiscard]] std::tuple<SocketResult, int64_t> send(const std::string_view& data) const noexcept override;

    [[nodiscard]] std::tuple<SocketResult, DataPtr> receive() const noexcept override;

    void close() noexcept override;
};


} //end of namespace slark::http
