//
// Created by Nevermore on 2024/6/17.
// slark Socket
// Copyright (c) 2024 Nevermore All rights reserved.
//
#include "Socket.h"
#include "Time.hpp"
#include "Log.hpp"
#include <netinet/tcp.h>

namespace slark::http {

#define GetSelectFDSet(type1, type2, FD)  ((type1) == (type2) ? FD : nullptr)
#define GetSelectReadFDSet(type, FD)  GetSelectFDSet(type, SelectType::Read, FD)
#define GetSelectWriteFDSet(type, FD)  GetSelectFDSet(type, SelectType::Write, FD)
SocketResult select(SelectType type, Socket socket, int64_t timeout) {
    fd_set fdSet;
    FD_ZERO(&fdSet);
    FD_SET(socket, &fdSet);
    int maxFd = socket + 1;
    timeval selectTimeout {
        static_cast<int32_t>(timeout / 1000),
        static_cast<int32_t>((timeout % 1000) * 1000)
    };
    SocketResult result;
    int ret = ::select(maxFd, GetSelectReadFDSet(type, &fdSet),
                       GetSelectWriteFDSet(type, &fdSet),
                       nullptr, timeout >= 0 ? &selectTimeout : nullptr);
    if (ret == SocketError) {
        result.errorCode = GetLastError();
        if (result.errorCode == RetryCode ||
            result.errorCode == AgainCode ||
            result.errorCode == EWOULDBLOCK) {
            result.resultCode = ResultCode::Retry;
        }
        result.resultCode = result.errorCode == RetryCode ? ResultCode::Retry : ResultCode::Failed;
    } else if (ret == 0) {
        result.resultCode = ResultCode::Timeout;
    }
    return result;
}

ISocket::ISocket(IPVersion ipVersion)
: ipVersion_(ipVersion)
, socket_(socket(GetAddressFamily(ipVersion), SOCK_STREAM, IPPROTO_TCP)){

}

ISocket::ISocket(ISocket&& rhs) noexcept
: ipVersion_(rhs.ipVersion_)
, socket_(std::exchange(rhs.socket_, kInvalidSocket)) {

}

ResultCode ISocket::config() noexcept {
    if (socket_ == kInvalidSocket) {
        return ResultCode::CreateSocketFailed;
    }
    ResultCode resultCode = ResultCode::Success;
    do {
        auto flags = fcntl(socket_, F_GETFL);
        if (flags == -1) {
            resultCode = ResultCode::GetFlagsFailed;
            break;
        }

        if (fcntl(socket_, F_SETFL, flags | O_NONBLOCK) == kInvalid) {
            resultCode = ResultCode::SetFlagsFailed;
            break;
        }
#if  defined(__APPLE__)
        int32_t value = 1;
        if (setsockopt(socket_, SOL_SOCKET, SO_NOSIGPIPE, &value, sizeof(value)) == kInvalid) {
            resultCode = ResultCode::SetNoSigPipeFailed;
        }
#endif
    } while(false);
    if (resultCode != ResultCode::Success) {
        close();
    }
    return resultCode;
}

ISocket& ISocket::operator=(ISocket&& rhs) noexcept {
    if (this == &rhs) {
        return *this;
    }
    close();
    ipVersion_ = rhs.ipVersion_;
    socket_ = std::exchange(rhs.socket_, kInvalidSocket);
    return *this;
}

void ISocket::close() noexcept {
    if (socket_ == kInvalidSocket) {
        return;
    }
    shutdown(socket_, SHUT_RDWR);
    ::close(socket_);
    socket_ = kInvalidSocket;
}

SocketResult ISocket::canSend(int64_t timeout) const noexcept {
    return select(SelectType::Write, socket_, timeout);
}

SocketResult ISocket::canReceive(int64_t timeout) const noexcept {
    return select(SelectType::Read, socket_, timeout);
}

void ISocket::checkConnectResult(SocketResult& result, int64_t timeout) const noexcept {
    if (result.isSuccess()) {
        return;
    }

    auto checkNeedRetry = [&result](){
        return result.errorCode == RetryCode || result.errorCode == BusyCode;
    };

    if (!checkNeedRetry()) {
        return;
    }

    auto expiredTime = Time::nowTimeStamp() + std::chrono::milliseconds(timeout);
    bool isTimeout = false;
    do {
        auto remainTime = static_cast<int64_t>((expiredTime - Time::nowTimeStamp()).point());
        if (remainTime < 0) {
            isTimeout = true;
            break;
        }
        auto selectResult = select(SelectType::Write, socket_, remainTime);
        result = selectResult;
    } while (!result.isSuccess() && checkNeedRetry());

    if (isTimeout) {
        result.resultCode = ResultCode::Timeout;
        result.errorCode = 0;
        return;
    }

    int error = 0;
    auto socketError = &error;
    auto errorLength = static_cast<socklen_t>(sizeof(int));
    if (getsockopt(socket_, SOL_SOCKET, SO_ERROR, socketError, &errorLength) == SocketError) {
        result.resultCode = ResultCode::ConnectGenericError;
        result.errorCode = GetLastError();
        return;
    }

    if (error != 0) {
        result.resultCode = ResultCode::ConnectGenericError;
        result.errorCode = error;
    } else {
        result.resultCode = ResultCode::Success;
        result.errorCode = 0;
    }
}

SocketResult ISocket::connect(const AddressInfoPtr& address, int64_t timeout) noexcept {
    SocketResult result;
    if (address == nullptr) {
        result.resultCode = ResultCode::ConnectAddressError;
        return result;
    }
    if (address->ai_family != GetAddressFamily(ipVersion_)) {
        result.resultCode = ResultCode::ConnectTypeInconsistent;
        return result;
    }
    result.resultCode = ISocket::config();
    if (!result.isSuccess()) {
        result.errorCode = GetLastError();
        return result;
    }

    auto connectResult = ::connect(socket_, address->ai_addr, address->ai_addrlen);
    if (connectResult == kInvalid) {
        result.errorCode = GetLastError();
    } else {
        result.errorCode = 0;
    }
    result.resultCode = connectResult == kInvalid ? ResultCode::ConnectGenericError : ResultCode::Success;
    checkConnectResult(result, timeout);
    return result;
}

#ifdef __APPLE__
#define TCP_KEEPIDLE TCP_KEEPALIVE
#endif
bool ISocket::setKeepLive() const noexcept {
    if (socket_ == kInvalidSocket) {
        return false;
    }
    int optval = 1;
    socklen_t optlen = sizeof(optval);

    if (setsockopt(socket_, SOL_SOCKET, SO_KEEPALIVE, &optval, optlen) < 0) {
        LogE("setsockopt SO_KEEPALIVE");
        return false;
    }

    optval = 60; // 60s
    if (setsockopt(socket_, IPPROTO_TCP, TCP_KEEPIDLE, &optval, optlen) < 0) {
        LogE("setsockopt TCP_KEEPIDLE");
        return false;
    }

    optval = 3;
    if (setsockopt(socket_, IPPROTO_TCP, TCP_KEEPCNT, &optval, optlen) < 0) {
        LogE("setsockopt TCP_KEEPCNT");
        return false;
    }

    optval = 10; // 10s
    if (setsockopt(socket_, IPPROTO_TCP, TCP_KEEPINTVL, &optval, optlen) < 0) {
        LogE("setsockopt TCP_KEEPINTVL");
        return false;
    }
    return true;
}

bool ISocket::isLive() const noexcept {
    fd_set read_fds, write_fds, except_fds;
    FD_ZERO(&read_fds);
    FD_ZERO(&write_fds);
    FD_ZERO(&except_fds);

    FD_SET(socket_, &read_fds);
    FD_SET(socket_, &write_fds);
    FD_SET(socket_, &except_fds);

    struct timeval timeout {
        .tv_sec = 0,
        .tv_usec = 0
    };

    int result = select(socket_ + 1, &read_fds, &write_fds, &except_fds, &timeout);
    if (result < 0) {
        LogE("select error:{}", result);
        return false;
    }

    if (result == 0) {
        return true;
    }

    if (FD_ISSET(socket_, &except_fds)) {
        return false;
    }

    if (FD_ISSET(socket_, &read_fds) || FD_ISSET(socket_, &write_fds)) {
        char buffer;
        ssize_t n = recv(socket_, &buffer, sizeof(buffer), MSG_PEEK | MSG_DONTWAIT);
        if (n == 0) {
            return false;//peer close
        } else if (n < 0) {
            if (errno == EWOULDBLOCK || errno == EAGAIN) {
                return true;
            } else {
                return false;
            }
        } else {
            return true;
        }
    }
    return true;
}

}//end of namespace slark::http
