//
// Created by Nevermore on 2024/5/16.
// slark request
// Copyright (c) 2024 Nevermore All rights reserved.
#pragma once

#include "Type.h"
#include "TimerManager.h"

#if ENABLE_HTTPS
#include "HttpsHelper.h"
#endif

namespace slark::http {

using namespace slark;
class ISocket;

struct Url;

extern void freeSocket(ISocket*) noexcept;

extern void freeUrl(Url*) noexcept;

class Request {
public:
    ///Data copying may result in some performance degradation
    [[maybe_unused]] explicit Request(const RequestInfo&, const ResponseHandler& );

    [[maybe_unused]] explicit Request(RequestInfo&&, ResponseHandler&&);

    ~Request();

    [[maybe_unused]] void cancel() noexcept {
        if (timerId_.isValid()) {
            TimerManager::shareInstance().cancel(timerId_);
            timerId_ = TimerId::kInvalidTimerId;
        }
        isValid_ = false;
        isCompleted_ = true;
    }

    [[maybe_unused]] [[nodiscard]] const std::string& getReqId() const {
        return info_.reqId;
    }
    
    bool isCompleted() const noexcept {
        return isCompleted_;
    }
    
    bool isValid() const noexcept {
        return isValid_;
    }
private:
    void config() noexcept;

    void sendRequest() noexcept;

    void redirect(const std::string&) noexcept;

    void process() noexcept;

    int64_t getRemainTime() const noexcept;

    bool send() noexcept;

    bool isReceivable() noexcept;

    void receive() noexcept;

    bool parseChunk(DataPtr& data, int64_t& chunkSize, bool& isCompleted) noexcept;

    void onResponseHeader(ResponseHeader&&) noexcept;

    void onResponseData(DataPtr data) noexcept;

    void onError(ResultCode code, int32_t errorCode) noexcept;

    void onCompleted() noexcept;
private:
    std::atomic<bool> isCompleted_ = false;
    std::atomic<bool> isValid_ = true;
    uint8_t redirectCount_ = 0;
    uint64_t startStamp_ = 0;
    RequestInfo info_;
    ResponseHandler handler_;
    std::unique_ptr<ISocket, decltype(&freeSocket)> socket_;
    std::unique_ptr<Url, decltype(&freeUrl)> url_;
    std::unique_ptr<std::thread> worker_ = nullptr;
    TimerId timerId_{};
};




}//end of namespace http

