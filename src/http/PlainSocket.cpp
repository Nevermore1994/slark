//
// Created by Nevermore on 2024/5/10.
// http-request Socket
// Copyright (c) 2024 Nevermore All rights reserved.
//
#include "PlainSocket.h"
#include "Socket.h"
#include "Type.h"

namespace slark::http {

PlainSocket::PlainSocket(IPVersion ipVersion)
    : ISocket(ipVersion) {

}

PlainSocket::~PlainSocket() {
    if (socket_ == kInvalidSocket) {
        return;
    }
    ::close(socket_);
    socket_ = kInvalidSocket;
}

SocketResult PlainSocket::connect(const AddressInfoPtr& address, int64_t timeout) noexcept {
    return ISocket::connect(address, timeout);
}

std::tuple<SocketResult, int64_t> PlainSocket::send(const std::string_view& dataView) const noexcept {
    int32_t sendSize = 0;
    SocketResult result;
    int32_t retryCount = 0;
    do {
        retryCount++;
        sendSize = static_cast<int32_t>(::send(socket_, dataView.data(), dataView.length(), kNoSignal));
        if (sendSize == SocketError) {
            result.errorCode = GetLastError();
        } else {
            result.errorCode = 0;
        }
    } while (retryCount < kMaxRetryCount && result.errorCode == RetryCode);

    if (sendSize == kInvalid) {
        sendSize = 0;
        result.resultCode = result.errorCode == RetryCode ? ResultCode::RetryReachMaxCount : ResultCode::Failed;
    } else if (sendSize == 0) {
        result.resultCode = ResultCode::Disconnected;
    }
    return {result, sendSize};
}

std::tuple<SocketResult, DataPtr> PlainSocket::receive() const noexcept {
    SocketResult result;
    auto data = std::make_unique<Data>(kDefaultReadSize);
    int32_t receiveSize = 0;
    int32_t retryCount = 0;
    do {
        retryCount++;
        auto dataPtr = reinterpret_cast<char*>(data->rawData);
        receiveSize = static_cast<int32_t>(::recv(socket_, dataPtr, data->capacity, kNoSignal));
        if (receiveSize == kInvalid) {
            result.errorCode = GetLastError();
        } else {
            data->length = static_cast<uint64_t>(receiveSize);
            result.errorCode = 0;
            break;
        }
    } while(retryCount < kMaxRetryCount && result.errorCode == RetryCode);
    if (receiveSize == kInvalid) {
        if (result.errorCode == RetryCode) {
            result.resultCode = ResultCode::RetryReachMaxCount;
        } else if (result.errorCode == AgainCode) {
            result.resultCode = ResultCode::Retry;
        } else {
            result.resultCode = ResultCode::Failed;
        }
    } else if (data->empty()) {
        result.resultCode = ResultCode::Disconnected;
    } else {
        result.resultCode = ResultCode::Success;
    }
    return {result, std::move(data)};
}

void PlainSocket::close() noexcept {
    ISocket::close();
}

} //end of namespace slark::http
