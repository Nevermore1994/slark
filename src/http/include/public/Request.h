//
// Created by Nevermore on 2024/5/16.
// slark request
// Copyright (c) 2024 Nevermore All rights reserved.
#pragma once

#include <thread>
#include <atomic>
#include <condition_variable>
#include <deque>
#include "Data.hpp"
#include "Type.h"

#if ENABLE_HTTPS
#include "HttpsHelper.h"
#endif

namespace slark::http {

using namespace slark;
class ISocket;

struct Url;

extern void freeSocket(ISocket*) noexcept;

extern void freeUrl(Url*) noexcept;

struct ResponseHeader;

struct RequestInfo {
    ///default true
    bool isAllowRedirect = true;
    ///default V4
    IPVersion ipVersion = IPVersion::Auto;
    std::string url;
    HttpMethodType methodType = HttpMethodType::Unknown;
    std::unordered_map<std::string, std::string> headers = {
        {"User-Agent",   "(KHTML, like Gecko)"},
        {"Accept",       "*/*"}};
    DataRefPtr body = nullptr;
    ///default 60s
    std::chrono::milliseconds timeout{60 * 1000};
    ///request id, Automatically generate
    std::string reqId;
    ///info tag
    std::string tag;

    [[nodiscard]] inline uint64_t bodySize() const noexcept {
        return body ? body->length : 0;
    }

    [[nodiscard]] inline bool bodyEmpty() const noexcept {
        return !body || body->empty();
    }
};

using RequestInfoPtr = std::unique_ptr<RequestInfo>;

struct ErrorInfo {
    ResultCode retCode = ResultCode::Success;
    int32_t errorCode{};

    ErrorInfo() = default;
    ErrorInfo(ResultCode resultCode, int32_t error)
        : retCode(resultCode)
        , errorCode(error) {

    }
};

struct ResponseHeader {
    std::unordered_map<std::string, std::string> headers;
    HttpStatusCode httpStatusCode = HttpStatusCode::Unknown;
    std::string reasonPhrase;

    [[nodiscard]] bool isNeedRedirect() const noexcept {
        return httpStatusCode == HttpStatusCode::MovedPermanently || httpStatusCode == HttpStatusCode::Found;
    }

    ResponseHeader() = default;
};

struct ResponseHandler {
    ///reqId
    ///Will not be called in the RequestSession
    using OnConnectedFunc = std::function<void(const RequestInfo&)>;
    OnConnectedFunc onConnected = nullptr;

    ///reqId, ResponseHeader
    using ParseHeaderDoneFunc = std::function<void(const RequestInfo&, ResponseHeader&&)>;
    ParseHeaderDoneFunc onParseHeaderDone = nullptr;

    ///reqId, data
    using ResponseDataFunc = std::function<void(const RequestInfo&, DataPtr data)>;
    ResponseDataFunc onData = nullptr;

    ///reqId
    using OnCompletedFunc = std::function<void(const RequestInfo&)>;
    OnCompletedFunc onCompleted = nullptr;

    ///reqId, ErrorInfo
    using OnErrorFunc = std::function<void(const RequestInfo&, ErrorInfo)>;
    OnErrorFunc onError = nullptr;
};

class Request {
public:
    ///Data copying may result in some performance degradation
    [[maybe_unused]] explicit Request(const RequestInfo&, const ResponseHandler& );

    [[maybe_unused]] explicit Request(RequestInfo&&, ResponseHandler&&);
    ~Request();

    [[maybe_unused]] void cancel() noexcept {
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
};

struct RequestSessionTask {
    std::atomic<bool> isCompleted_ = false;
    std::atomic<bool> isValid_ = true;
    std::atomic<bool> isRedirect_ = false;
    std::unique_ptr<Url, decltype(&freeUrl)> host{nullptr, &freeUrl};
    uint64_t startStamp = 0;
    RequestInfoPtr requestInfo = nullptr;
};

class RequestSession {
public:
    RequestSession(std::unique_ptr<ResponseHandler> handler);
    
    ~RequestSession();
    
    void close() noexcept;
    
    std::string request(RequestInfoPtr requestInfo) noexcept;
    
    bool isBusy() const noexcept {
        return isBusy_;
    }

private:
    void setupSocket() noexcept;
    void redirect(const std::string&) noexcept;
    void process() noexcept;
    int64_t getRemainTime() noexcept;
    bool send() noexcept;
    bool isReceivable() noexcept;
    void receive() noexcept;
    bool parseChunk(DataPtr& data, int64_t& chunkSize, bool& isCompleted) noexcept;
    void onResponseHeader(ResponseHeader&&) noexcept;
    void onResponseData(DataPtr data) noexcept;
    void onError(ResultCode code, int32_t errorCode) noexcept;
    void onCompleted() noexcept;
private:
    std::mutex mutex_; //64
    std::mutex taskMutex_; //64
    std::atomic<bool> isExited_ = false;//1
    std::atomic<bool> isBusy_ = false; //1
    std::condition_variable cond_;//48
    std::unique_ptr<ISocket, decltype(&freeSocket)> socket_; //16
    std::unique_ptr<Url, decltype(&freeUrl)> host_; //16
    std::unique_ptr<std::thread> worker_ = nullptr; //8
    std::unique_ptr<ResponseHandler> handler_ = nullptr;//8
    std::deque<std::unique_ptr<RequestSessionTask>> requestTaskQueue_;
    std::unique_ptr<RequestSessionTask> currentTask_ = nullptr;
};

}//end of namespace http

