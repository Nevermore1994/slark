//
// Created by Nevermore on 2024/5/16.
// slark request
// Copyright (c) 2024 Nevermore All rights reserved.
#pragma once

#include <thread>
#include <atomic>
#include "Data.hpp"
#include "Type.h"

#if ENABLE_HTTPS
#include "HttpsHelper.h"
#endif

namespace slark::http {

using namespace slark;
class ISocket;

class Url;

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
    std::unordered_map<std::string, std::string> headers;
    DataRefPtr body = nullptr;
    ///default 30s
    std::chrono::milliseconds timeout{60 * 1000};

    [[nodiscard]] inline uint64_t bodySize() const noexcept {
        return body ? body->length : 0;
    }

    [[nodiscard]] inline bool bodyEmpty() const noexcept {
        return !body || body->empty();
    }
};

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
    using OnConnectedFunc = std::function<void(std::string_view)>;
    OnConnectedFunc onConnected = nullptr;

    ///reqId, ResponseHeader
    using ParseHeaderDoneFunc = std::function<void(std::string_view, ResponseHeader&&)>;
    ParseHeaderDoneFunc onParseHeaderDone = nullptr;

    ///reqId, data
    using ResponseDataFunc = std::function<void(std::string_view, DataPtr data)>;
    ResponseDataFunc onData = nullptr;

    ///reqId
    using OnDisconnectedFunc = std::function<void(std::string_view)>;
    OnDisconnectedFunc onDisconnected = nullptr;

    ///reqId, ErrorInfo
    using OnErrorFunc = std::function<void(std::string_view, ErrorInfo)>;
    OnErrorFunc onError = nullptr;
};

class Request {
public:
    static bool init();
    static void clear();

public:
    ///Data copying may result in some performance degradation
    [[maybe_unused]] explicit Request(const RequestInfo&, const ResponseHandler& );

    [[maybe_unused]] explicit Request(RequestInfo&&, ResponseHandler&&);
    ~Request();

    [[maybe_unused]] void cancel() noexcept {
        isValid_ = false;
    }

    [[maybe_unused]] [[nodiscard]] const std::string& getReqId() const {
        return reqId_;
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
    void responseHeader(ResponseHeader&&) noexcept;
    void responseData(DataPtr data) noexcept;
    void handleErrorResponse(ResultCode code, int32_t errorCode) noexcept;
    void disconnected() noexcept;
private:
    uint8_t redirectCount_ = 0;
    std::atomic<bool> isValid_ = true;
    uint64_t startStamp_ = 0;
    RequestInfo info_;
    ResponseHandler handler_;
    std::unique_ptr<ISocket, decltype(&freeSocket)> socket_;
    std::unique_ptr<Url, decltype(&freeUrl)> url_;
    std::unique_ptr<std::thread> worker_ = nullptr;
    std::string reqId_;
};

}//end of namespace http

