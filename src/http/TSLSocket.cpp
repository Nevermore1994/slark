//
// Created by Nevermore on 2024/6/17.
// slark TSLSocket
// Copyright (c) 2024 Nevermore All rights reserved.
//
#if ENABLE_HTTPS
#include "TSLSocket.h"
#include "SSLManager.h"
#include "Socket.h"
#include "Type.h"

namespace slark::http {

TSLSocket::TSLSocket(IPVersion ipVersion) : ISocket(ipVersion), sslPtr(SSLManager::create(socket_)) {

}

TSLSocket::~TSLSocket() {
    TSLSocket::close();
}

SocketResult TSLSocket::connect(const AddressInfoPtr& address, int64_t timeout) noexcept {
    SocketResult result = ISocket::connect(address, timeout);
    if (!result.isSuccess()) {
        return result;
    }
    return SSLManager::connect(sslPtr);
}

std::tuple<SocketResult, int64_t> TSLSocket::send(const std::string_view& data) const noexcept {
    SocketResult result;
    if (sslPtr == nullptr) {
        result.resultCode = ResultCode::Failed;
        return {result, 0};
    }
    return SSLManager::write(sslPtr, data);
}

std::tuple<SocketResult, DataPtr> TSLSocket::receive() const noexcept {
    SocketResult result;
    if (sslPtr == nullptr) {
        result.resultCode = ResultCode::Failed;
        return {result, nullptr};
    }
    return SSLManager::read(sslPtr);
}

void TSLSocket::close() noexcept {
    SSLManager::close(sslPtr);
    ISocket::close();
    sslPtr.reset();
}

SocketResult TSLSocket::canSend(int64_t timeout) const noexcept {
    return ISocket::canSend(timeout);
}

SocketResult TSLSocket::canReceive(int64_t timeout) const noexcept {
    if (SSL_pending(sslPtr.get()) > 0) {
        return {};
    }
    return ISocket::canReceive(timeout);
}

} //end of namespace slark::http

#endif
