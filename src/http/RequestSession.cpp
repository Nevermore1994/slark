//
// Created by Nevermore- on 2025/6/24.
//

#include <cstdint>
#include <string_view>
#include <utility>
#include <ios>
#include "Random.hpp"
#include "Time.hpp"
#include "Util.hpp"
#include "Log.hpp"
#include "include/HttpUtil.h"
#include "include/public//RequestSession.h"
#include "include/TSLSocket.h"
#include "include/public/Type.h"
#include "include/PlainSocket.h"
#include "include/Url.h"

#define MakeUrlPtr(url) std::unique_ptr<Url, decltype(&freeUrl)>(new Url(url), freeUrl)

namespace slark::http {

RequestSession::RequestSession(std::unique_ptr<ResponseHandler> handler)
    : socket_(nullptr, freeSocket)
    , host_(nullptr, freeUrl)
    , worker_(std::make_unique<std::thread>(&RequestSession::process, this))
    , handler_(std::move(handler)) {

}

RequestSession::~RequestSession() {
    if (!isExited_) {
        close();
    }
    if (worker_->joinable()) {
        worker_->join();
    }
    worker_.reset();
}

void RequestSession::clear() noexcept {
    {
        std::lock_guard<std::mutex> lock(taskMutex_);
        requestTaskQueue_.clear();
    }
    {
        std::lock_guard<std::mutex> lock(mutex_);
        handler_.reset();
    }
    if (currentTask_) {
        currentTask_->isValid_ = false;
    }
    cond_.notify_all();
    LogI("RequestSession clear");
}

void RequestSession::close() noexcept {
    isExited_ = true;
    clear();
    LogI("RequestSession close");
}

void RequestSession::setupSocket() noexcept {
    if (!host_ || !host_->isValid()) {
        return;
    }
    addrinfo hints{};
    hints.ai_family = GetAddressFamily(IPVersion::Auto);
    hints.ai_socktype = SOCK_STREAM; //tcp
    addrinfo* addressInfo = nullptr;
    ResponseHeader responseData;
    LogI("host:{}, port:{}", host_->host, host_->port);
    if (getaddrinfo(host_->host.data(), host_->port.data(), &hints, &addressInfo) != 0 ||
        addressInfo == nullptr) {
        LogE("get addr info failed.");
        return;
    }
    auto ipVersion = addressInfo->ai_family == AF_INET ? IPVersion::V4 : IPVersion::V6;
    ISocket* socketPtr = nullptr;
    if (host_->isHttps()) {
#if ENABLE_HTTPS
        socketPtr = new TSLSocket(ipVersion);
#else
        LogE("scheme not support");
        return;
#endif
    } else {
        socketPtr = new PlainSocket(ipVersion);
    }
    socket_ = std::unique_ptr<ISocket, decltype(&freeSocket)>(socketPtr, &freeSocket);
    auto addressInfoPtr = MakeAddressInfoPtr(addressInfo);
    if (socket_->setKeepLive()) {
        LogI("set keep live success");
    } else {
        LogE("set keep live failed");
    }
    auto result = socket_->connect(addressInfoPtr, 60000); //60s
    if (result.isSuccess()) {
        LogI("connect success:{}", host_->host);
    } else {
        LogE("connect failed:{}", result.errorCode);
    }
}

void RequestSession::process() noexcept {
    while (true) {
        std::string host;
        if (currentTask_ && currentTask_->isRedirect_) {
            currentTask_->isRedirect_ = false;
        } else {
            currentTask_.reset();
            std::unique_lock<std::mutex> lock(taskMutex_);
            cond_.wait(lock, [this](){ return !requestTaskQueue_.empty() || isExited_; });
            if (isExited_ || requestTaskQueue_.empty()) {
                return;
            }
            currentTask_ = std::move(requestTaskQueue_.front());
            requestTaskQueue_.pop_front();
        }
        if (!currentTask_) {
            LogE("error task");
            continue;
        }
        if (!currentTask_->host) {
            currentTask_->host = MakeUrlPtr(currentTask_->requestInfo->url);
        }
        isBusy_ = true;
        LogI("current task url:{}", currentTask_->requestInfo->url);
        if (!host_ || (host_ && host_->host != currentTask_->host->host) ) {
            LogI("host change");
            host_ = std::move(currentTask_->host);
            socket_.reset();
        }
        if (socket_ == nullptr || !socket_->isLive()) {
            setupSocket();
        }
        if (!send()) {
            continue;
        }
        receive();
    }
}

bool RequestSession::send() noexcept {
    using namespace std::chrono_literals;
    auto canSend = socket_->canSend(getRemainTime());
    if (!currentTask_ || !canSend.isSuccess()) {
        onError(canSend.resultCode, canSend.errorCode);
        return false;
    }
    std::this_thread::sleep_for(1ms);
    currentTask_->requestInfo->headers["Connection"] = "keep-alive";
    if (!currentTask_->host) {
        currentTask_->host = std::unique_ptr<Url, decltype(&freeUrl)>(new Url(currentTask_->requestInfo->url), freeUrl);
    }
    auto sendData = Util::htmlEncode(*currentTask_->requestInfo, *currentTask_->host);
    auto dataView = std::string_view(sendData);
    LogI("send data: {}", dataView);
    do {
        auto [sendResult, sendSize] = socket_->send(dataView);
        if (!sendResult.isSuccess()) {
            onError(sendResult.resultCode, sendResult.errorCode);
            return false;
        } else if (sendSize < static_cast<int64_t>(dataView.size())) {
            dataView = dataView.substr(static_cast<uint64_t>(sendSize));
        } else {
            break;
        }
    } while (!dataView.empty());
    return true;
}

std::string RequestSession::request(RequestInfoPtr requestInfo) noexcept {
    auto taskPtr = std::make_unique<RequestSessionTask>();
    auto host = std::unique_ptr<Url, decltype(&freeUrl)>(new Url(requestInfo->url), freeUrl);
    if (!host->isValid()) {
        LogE("host is invalid");
        return "";
    }
    if (!host->isSupportScheme()) {
        LogE("scheme not support");
        return "";
    }
    taskPtr->requestInfo = std::move(requestInfo);
    taskPtr->host = std::move(host);
    auto reqId = Random::randomString(12);
    taskPtr->requestInfo ->reqId = reqId;
    taskPtr->startStamp = Time::nowTimeStamp().point();
    {
        std::lock_guard<std::mutex> lock(taskMutex_);
        requestTaskQueue_.push_back(std::move(taskPtr));
        cond_.notify_all();
    }
    LogI("send request:{}", reqId);
    return reqId;
}

bool RequestSession::isReceivable() noexcept {
    while (true) {
        if (!currentTask_->isValid_) {
            return false;
        }
        if (isExited_) {
            LogI("session close");
            return false;
        }
        auto timeout = getRemainTime();
        LogI("remain time:{}", timeout);
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

bool RequestSession::parseChunk(DataPtr& data, int64_t& chunkSize, bool& isCompleted) noexcept {
    return Util::parseChunkHandler(data, chunkSize, isCompleted,
     [this](ResultCode resultCode, int32_t errorCode){
         onError(resultCode, errorCode);
     },
     [this](DataPtr data) {
         onResponseData(std::move(data));
     });
}

void RequestSession::redirect(const std::string& host) noexcept {
    LogI("redirect:{}", host);
    bool isSuccess = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        host_ = MakeUrlPtr(host);
        if (!host_->isValid() || !host_->isSupportScheme()) {
            onError(ResultCode::RedirectError, 0);
        } else {
            isSuccess = true;
        }
    }
    if (isSuccess) {
        currentTask_->isRedirect_ = true;
    }
}

void RequestSession::receive() noexcept {
    using namespace std::chrono_literals;
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
        if (!currentTask_->isValid_ || isExited_) {
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
                currentTask_->isCompleted_ = true;
                onCompleted();
            } else {
                onError(recvResult.resultCode, recvResult.errorCode);
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
            if (response.isNeedRedirect() && currentTask_->requestInfo->isAllowRedirect) {
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
            currentTask_->isCompleted_ = true;
            onCompleted();
            return; //disconnect
        }
    }
}

void RequestSession::onResponseHeader(ResponseHeader&& header) noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    if (currentTask_ && handler_ && handler_->onParseHeaderDone) {
        handler_->onParseHeaderDone(*currentTask_->requestInfo, std::move(header));
    }
}

void RequestSession::onResponseData(DataPtr data) noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    if (currentTask_ && handler_ && handler_->onData) {
        handler_->onData(*currentTask_->requestInfo, std::move(data));
    }
}

void RequestSession::onError(ResultCode code, int32_t errorCode) noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    if (currentTask_ && handler_ && handler_->onError) {
        handler_->onError(*currentTask_->requestInfo, {code, errorCode});
    }
}

void RequestSession::onCompleted() noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    isBusy_ = false;
    if (currentTask_ && currentTask_->isValid_ &&
        handler_ && handler_->onCompleted) {
        handler_->onCompleted(*currentTask_->requestInfo);
    }
}

int64_t RequestSession::getRemainTime() noexcept {
    if (!currentTask_) {
        return 0;
    }
    auto t = Time::nowTimeStamp() - Time::TimePoint(currentTask_->startStamp);
    auto diff = currentTask_->requestInfo->timeout - t.toMilliSeconds();
    return std::max<int64_t>(diff.count(), 0ll);
}

} // slark