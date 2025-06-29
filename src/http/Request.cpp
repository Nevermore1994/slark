//
// Created by Nevermore on 2024/5/16.
// slark Request
// Copyright (c) 2024 Nevermore All rights reserved.
//
#include <cstdint>
#include <string_view>
#include <utility>
#include <ios>
#include "Request.h"
#include "TSLSocket.h"
#include "Type.h"
#include "PlainSocket.h"
#include "Url.h"
#include "Random.hpp"
#include "Time.hpp"
#include "Util.hpp"
#include "Log.hpp"
#include "HttpUtil.h"

namespace slark::http {

using namespace std::chrono_literals;

Request::Request(const RequestInfo& info, const ResponseHandler& responseHandler)
    : startStamp_(Time::nowTimeStamp().point())
    , info_(info)
    , handler_(responseHandler)
    , socket_(nullptr, freeSocket)
    , url_(nullptr, freeUrl) {
    config();
}

Request::Request(RequestInfo&& info, ResponseHandler&& responseHandler)
    : startStamp_(Time::nowTimeStamp().point())
    , info_(std::move(info))
    , handler_(std::move(responseHandler))
    , socket_(nullptr,freeSocket)
    , url_(nullptr, freeUrl) {
    config();
}

Request::~Request() {
    isValid_ = false;
    if (worker_ && worker_->joinable()) {
        worker_->join();
    }
}

void Request::config() noexcept {
    info_.reqId = Random::randomString(20);
    worker_ = std::make_unique<std::thread>(&Request::process, this);
}

#ifdef __clang__
#pragma clang diagnostic push
#pragma ide diagnostic ignored "misc-no-recursion"
#endif

void Request::sendRequest() noexcept {
    auto errorHandler = [&](ResultCode code, int32_t errorCode) {
        this->onError(code, errorCode);
    };
    addrinfo hints{};
    hints.ai_family = GetAddressFamily(info_.ipVersion);
    hints.ai_socktype = SOCK_STREAM; //tcp
    addrinfo* addressInfo = nullptr;
    ResponseHeader responseData;
    auto addrCode = getaddrinfo(url_->host.data(), url_->port.data(), &hints, &addressInfo);
    if (addrCode != 0 || addressInfo == nullptr) {
        int lastError = GetLastError();
        auto errorMessage = std::string(gai_strerror(addrCode));
        LogE("get addr info failed: {}, last error: {}", errorMessage, lastError);
        errorHandler(ResultCode::GetAddressFailed, lastError);
        return;
    }
    auto ipVersion = info_.ipVersion;
    if (ipVersion == IPVersion::Auto) {
        ipVersion = addressInfo->ai_family == AF_INET ? IPVersion::V4 : IPVersion::V6;
    }
    ISocket* socketPtr = nullptr;
    if (url_->isHttps()) {
#if ENABLE_HTTPS
        socketPtr = new TSLSocket(ipVersion);
#else
        errorHandler(ResultCode::SchemeNotSupported, 0);
#endif
    } else {
        socketPtr = new PlainSocket(ipVersion);
    }
    socket_ = std::unique_ptr<ISocket, decltype(&freeSocket)>(socketPtr, &freeSocket);
    auto addressInfoPtr = MakeAddressInfoPtr(addressInfo);
    auto timeout = getRemainTime();
    if (timeout <= 0) {
        errorHandler(ResultCode::Timeout, GetLastError());
        return;
    }
    auto result = socket_->connect(addressInfoPtr, timeout);
    if (!result.isSuccess()) {
        LogE("connect {} failed, error code: {}, result code: {}",
             url_->url(), result.errorCode, static_cast<int>(result.resultCode));
        errorHandler(result.resultCode, GetLastError());
        return;
    } else if (isValid_ && handler_.onConnected) {
        handler_.onConnected(info_);
    }
    if (!send()) {
        return;
    }
    receive();
}

void Request::redirect(const std::string& url) noexcept {
    auto errorHandler = [this](ResultCode code) {
        onError(code, 0);
    };
    constexpr uint8_t redirectMaxCount = 7;
    if (url.empty()) {
        errorHandler(ResultCode::RedirectError);
        return;
    }
    if (redirectCount_ >= redirectMaxCount) {
        errorHandler(ResultCode::RedirectReachMaxCount);
    }
    redirectCount_++;
    url_ = std::unique_ptr<Url, decltype(&freeUrl)>(new Url(url), freeUrl);
    if (!url_->isValid() || !url_->isSupportScheme()) {
        return errorHandler(ResultCode::RedirectError);
    }
    sendRequest();
}

void Request::process() noexcept {
//    timerId_ = TimerManager::shareInstance().runAfter(info_.timeout, [this]{
//        onError(ResultCode::Timeout, 0);
//    });
    auto handler = [&](ResultCode code) {
        this->onError(code, 0);
    };
    if (info_.methodType == HttpMethodType::Unknown) {
        handler(ResultCode::MethodError);
        return;
    }

    url_ = std::unique_ptr<Url, decltype(&freeUrl)>(new Url(info_.url), freeUrl);
    if (!url_->isValid()) {
        handler(ResultCode::UrlInvalid);
        return;
    }
    if (!url_->isSupportScheme()) {
        handler(ResultCode::SchemeNotSupported);
        return;
    }
    sendRequest();
}

bool Request::send() noexcept {
    auto canSend = socket_->canSend(getRemainTime());
    if (!canSend.isSuccess()) {
        onError(canSend.resultCode, canSend.errorCode);
        return false;
    }
    std::this_thread::sleep_for(1ms);
    auto sendData = Util::htmlEncode(info_, *url_);
    auto dataView = std::string_view(sendData);
    do {
        auto [sendResult, sendSize] = socket_->send(dataView);
        if (!sendResult.isSuccess()) {
            this->onError(sendResult.resultCode, sendResult.errorCode);
            return false;
        } else if (sendSize < static_cast<int64_t>(dataView.size())) {
            dataView = dataView.substr(static_cast<size_t>(sendSize));
        } else {
            break;
        }
    } while (!dataView.empty());
    return true;
}

bool Request::isReceivable() noexcept {
    while (true) {
        if (!isValid_) {
            onCompleted();
            return false;
        }
        auto timeout = getRemainTime();
        auto canReceive = socket_->canReceive(timeout);
        if (canReceive.isSuccess()) {
            return true;
        }
        timeout = getRemainTime();
        if (timeout > 0 && canReceive.resultCode == ResultCode::Retry) {
            continue;
        }
        onError(canReceive.resultCode, canReceive.errorCode);
        return false;//disconnect
    }
}

bool Request::parseChunk(DataPtr& data, int64_t& chunkSize, bool& isCompleted) noexcept {
    return Util::parseChunkHandler(data, chunkSize, isCompleted,
        [this](ResultCode resultCode, int32_t errorCode){
            onError(resultCode, errorCode);
        },
        [this](DataPtr data) {
            onResponseData(std::move(data));
        });
}

void Request::receive() noexcept {
    ResponseHeader response;
    auto recvDataPtr = std::make_unique<Data>();
    bool parseHeaderSuccess = false;
    int64_t contentLength = INT64_MAX;
    int64_t recvLength = 0;
    std::string transferCoding;
    int64_t chunkSize = kInvalid;
    while (true) {
        if (!isReceivable()) {
            return;
        }
        std::this_thread::sleep_for(1ms);
        if (!isValid_) {
            onCompleted();
            return;
        }
        auto [recvResult, dataPtr] = socket_->receive();
        bool isCompleted = (recvResult.resultCode == ResultCode::Completed ||
                            recvResult.resultCode == ResultCode::Disconnected);
        if (!recvResult.isSuccess()) {
            if (recvResult.resultCode == ResultCode::Retry) {
                continue;
            }
            if (isCompleted) {
                isCompleted_ = true;
                onCompleted();
            } else {
                this->onError(recvResult.resultCode, recvResult.errorCode);
            }
            return;
        }

        if (!parseHeaderSuccess) {
            recvDataPtr->append(std::move(dataPtr));
            auto [isSuccess, headerSize] = Util::parseResponseHeader(recvDataPtr->view(), response);
            parseHeaderSuccess = isSuccess;
            if (!isSuccess) {
                continue;
            }
            if (response.isNeedRedirect() && info_.isAllowRedirect) {
                redirect(response.headers["Location"]);
                return;
            }
            Util::parseFieldValue(response.headers, "Content-Length", contentLength);
            Util::parseFieldValue(response.headers, "Transfer-Encoding", transferCoding);
            onResponseHeader(std::move(response));
            dataPtr = recvDataPtr->copy(static_cast<uint64_t>(headerSize));
            recvDataPtr->reset();
        }

        recvLength += static_cast<int64_t>(dataPtr->length);
        if (transferCoding == "chunked") {
            recvDataPtr->append(std::move(dataPtr));
            if (!parseChunk(recvDataPtr, chunkSize, isCompleted)) {
                return; //disconnect
            }
        } else {
            isCompleted = isCompleted || recvLength >= contentLength;
            if (parseHeaderSuccess && !dataPtr->empty()){
                onResponseData(std::move(dataPtr));
            }
        }

        if (isCompleted) {
            isCompleted_ = true;
            onCompleted();
            return; //disconnect
        }
    }
}
#ifdef __clang__
#pragma clang diagnostic pop
#endif

int64_t Request::getRemainTime() const noexcept {
    auto t = Time::nowTimeStamp() - Time::TimePoint(startStamp_);
    auto diff = info_.timeout - t.toMilliSeconds();
    return std::max<int64_t>(diff.count(), 0ll);
}

void Request::onError(ResultCode code, int32_t errorCode) noexcept {
    if (isValid_ && handler_.onError) {
        handler_.onError(info_, {code, errorCode});
    }
    onCompleted();
}

void Request::onResponseHeader(ResponseHeader&& header) noexcept {
    if (isValid_ && handler_.onParseHeaderDone) {
        handler_.onParseHeaderDone(info_, std::move(header));
    }
}

void Request::onResponseData(DataPtr dataPtr) noexcept {
    if (isValid_ && handler_.onData) {
        handler_.onData(info_, std::move(dataPtr));
    }
}

void Request::onCompleted() noexcept {
    if (timerId_.isValid()) {
        TimerManager::shareInstance().cancel(timerId_);
        timerId_ = TimerId::kInvalidTimerId;
    }
    if (isValid_) {
        if (handler_.onCompleted) {
            handler_.onCompleted(info_);
        }
    }
    isValid_ = false;
    socket_.reset(); //release resource
}

} //end of namespace slark::http
