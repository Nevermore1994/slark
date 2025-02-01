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

namespace slark::http {

constexpr const char* kMethodNameArray[] = {"Unknown", "GET", "POST", "PUT", "PATCH", "DELETE", "OPTIONS"};

std::string getMethodName(HttpMethodType type) {
    return kMethodNameArray[static_cast<int>(type)];
}

template <typename T>
bool parseFieldValue(const std::unordered_map<std::string, std::string>& headers, const std::string& key, T& value) {
    if (headers.count(key) == 0) {
        return false;
    }
    if constexpr (std::is_same_v<double, T>) {
        value = std::stod(headers.at(key));
    } else if constexpr (std::is_same_v<float, T>) {
        value = std::stof(headers.at(key));
    } else if constexpr (std::is_same_v<uint32_t, T>) {
        value = std::stoul(headers.at(key));
    } else if constexpr (std::is_same_v<int32_t, T>) {
        value = std::stoi(headers.at(key));
    } else if constexpr (std::is_same_v<uint64_t, T>) {
        value = std::stoull(headers.at(key));
    } else if constexpr (std::is_same_v<int64_t, T>) {
        value = std::stoll(headers.at(key));
    } else if constexpr (std::is_same_v<std::string, T> || std::is_same_v<std::string_view, T>) {
        value = headers.at(key);
    } else if constexpr (std::is_same_v<bool, T>) {
        std::istringstream(headers.at(key)) >> std::boolalpha >> value;
    } else if constexpr (std::is_same_v<char, T>) {
        value = static_cast<char>(std::stoi(headers.at(key)));
    } else if constexpr (std::is_same_v<unsigned char, T>) {
        value = static_cast<unsigned char>(std::stoi(headers.at(key)));
    } else {
        return false;
    }
    return true;
}

namespace encode {
///https://stackoverflow.com/questions/180947/base64-decode-snippet-in-c
constexpr std::string_view kBase64Content = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/"sv;

std::string base64Encode(const std::string& str) {
    std::string res;
    res.reserve(str.length());
    int val = 0, valb = -6;
    for (const auto& c : str) {
        val = (val << 8) + c;
        valb += 8;
        while (valb >= 0) {
            res.push_back(kBase64Content[(val >> valb) & 0x3F]);
            valb -= 6;
        }
    }
    if (valb > -6) res.push_back(kBase64Content[((val << 8) >> (valb + 8)) & 0x3F]);
    while (res.size() % 4) res.push_back('=');
    return res;
}

std::string htmlEncode(RequestInfo& info, const Url& url) noexcept {
    auto& headers = info.headers;
    if (!info.bodyEmpty()) {
        headers["Content-Length"] = std::to_string(info.bodySize());
    }
    headers["Host"] = url.host;
    if (headers.count("Authorization") == 0 && !url.userInfo.empty()) {
        headers["Authorization"] = std::string("Basic ") + base64Encode(url.userInfo);
    }
    std::ostringstream oss;
    oss << getMethodName(info.methodType) << " " << url.path << (url.query.empty() ? "" : "?" + url.query)
        << " HTTP/1.1\r\n";
    std::for_each(headers.begin(), headers.end(), [&](const auto& pair) {
        oss << pair.first << ": " << pair.second << "\r\n";
    });
    if (!info.bodyEmpty()) {
        oss << "\r\n";
        oss << info.body->view();
    }
    oss << "\r\n";
    return oss.str();
}

}

using namespace std::chrono_literals;

Request::Request(const RequestInfo& info, const ResponseHandler& responseHandler)
    : startStamp_(Time::nowTimeStamp())
    , info_(info)
    , handler_(responseHandler)
    , socket_(nullptr, freeSocket)
    , url_(nullptr, freeUrl)
    , reqId_(Random::randomString(20)) {
    config();
}

Request::Request(RequestInfo&& info, ResponseHandler&& responseHandler)
    : startStamp_(Time::nowTimeStamp())
    , info_(std::move(info))
    , handler_(std::move(responseHandler))
    , socket_(nullptr,freeSocket)
    , url_(nullptr, freeUrl)
    , reqId_(Random::randomString(20)) {
    config();
}

Request::~Request() {
    isValid_ = false;
    if (worker_ && worker_->joinable()) {
        worker_->join();
    }
}

void Request::config() noexcept {
    worker_ = std::make_unique<std::thread>(&Request::process, this);
}

#ifdef __clang__
#pragma clang diagnostic push
#pragma ide diagnostic ignored "misc-no-recursion"
#endif

void Request::sendRequest() noexcept {
    auto errorHandler = [&](ResultCode code, int32_t errorCode) {
        this->handleErrorResponse(code, errorCode);
    };
    addrinfo hints{};
    hints.ai_family = GetAddressFamily(info_.ipVersion);
    hints.ai_socktype = SOCK_STREAM; //tcp
    addrinfo* addressInfo = nullptr;
    ResponseHeader responseData;
    if (getaddrinfo(url_->host.data(), url_->port.data(), &hints, &addressInfo) != 0 || addressInfo == nullptr) {
        errorHandler(ResultCode::GetAddressFailed, GetLastError());
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
    socket_ = std::unique_ptr<ISocket, decltype(&freeSocket)>(socketPtr, freeSocket);
    auto addressInfoPtr = MakeAddressInfoPtr(addressInfo);
    auto timeout = getRemainTime();
    if (timeout <= 0) {
        errorHandler(ResultCode::Timeout, GetLastError());
        return;
    }
    auto result = socket_->connect(addressInfoPtr, timeout);
    if (!result.isSuccess()) {
        errorHandler(result.resultCode, GetLastError());
        return;
    } else if (isValid_ && handler_.onConnected) {
        handler_.onConnected(reqId_);
    }
    if (!send()) {
        return;
    }
    receive();
}

void Request::redirect(const std::string& url) noexcept {
    auto errorHandler = [this](ResultCode code) {
        handleErrorResponse(code, 0);
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
    auto handler = [&](ResultCode code) {
        this->handleErrorResponse(code, 0);
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
        handleErrorResponse(canSend.resultCode, canSend.errorCode);
        return false;
    }
    std::this_thread::sleep_for(1ms);
    auto sendData = encode::htmlEncode(info_, *url_);
    auto dataView = std::string_view(sendData);
    do {
        auto [sendResult, sendSize] = socket_->send(dataView);
        if (!sendResult.isSuccess()) {
            this->handleErrorResponse(sendResult.resultCode, sendResult.errorCode);
            return false;
        } else if (sendSize < dataView.size()) {
            dataView = dataView.substr(sendSize);
        } else {
            break;
        }
    } while (!dataView.empty());
    return true;
}

std::tuple<bool, int64_t> parseResponseHeader(DataView data, ResponseHeader& response) {
    constexpr std::string_view kCRLF = "\r\n"sv;
    constexpr std::string_view kHeaderEnd = "\r\n\r\n"sv;
    ///https://www.rfc-editor.org/rfc/rfc7230#section-3.1.2:~:text=header%2Dfield%20CRLF%20)-,CRLF,-%5B%20message%2Dbody%20%5D
    auto headerEndPos = data.find(kHeaderEnd);
    if (headerEndPos == std::string_view::npos) {
        return {false, 0};
    }
    auto headerView = data.substr(0, headerEndPos);

    auto headerViews = headerView.split(kCRLF);
    ///parse version and status
    auto statusView = headerViews[0];
    constexpr std::string_view kHTTPFlag = "HTTP/"sv;
    if (auto versionPos = statusView.find(kHTTPFlag); versionPos != std::string_view::npos) {
        ///HTTP-version SP status-code SP reason-phrase CRLF
        response.headers["Version"] = statusView.substr(versionPos, kHTTPFlag.size() + 3).view();
        response.httpStatusCode = static_cast<HttpStatusCode>(std::stoi(statusView.substr(kHTTPFlag.size() + 4, 3).str()));
        response.reasonPhrase = statusView.substr(versionPos + kHTTPFlag.size() + 3 + 1 + 3 + 1).view();
    }
    for (auto& view : headerViews) {
        if (view.empty() || view.find(':') == std::string_view::npos) {
            continue;
        }
        auto fieldValue = view.split(": ");
        if (fieldValue.size() == 2) {
            auto& name = fieldValue[0];
            auto& value = fieldValue[1];
            response.headers[name.removePrefix(' ').str()] = value.removePrefix(' ').str();
        }
    }
    return {true, headerEndPos + kHeaderEnd.size()};
}

bool parseChunkHandler(DataPtr& data,
                int64_t& chunkSize,
                bool& isCompleted,
                const std::function<void(ResultCode, int32_t)>& errorHandler,
                const std::function<void(DataPtr)>& dataHandler) noexcept {
    constexpr std::string_view kCRLF = "\r\n"sv;
    bool res = true;
    auto dataView = data->view();
    while (!dataView.empty()) {
        if (chunkSize <= 0) {
            auto pos = dataView.find(kCRLF);
            if (pos == std::string_view::npos) {
                errorHandler(ResultCode::ChunkSizeError, 0);
                res = false;
                break;
            }
            chunkSize = std::stol(dataView.substr(0, pos).str(), nullptr, 16);
            if (chunkSize == 0) {
                isCompleted = true;
                break;
            }
            dataView = dataView.substr(pos + kCRLF.size());
        } else {
            auto size = std::min(static_cast<uint64_t>(chunkSize), dataView.length());
            dataHandler(std::make_unique<Data>(dataView.substr(0, size).view()));
            dataView = dataView.substr(size);
            chunkSize -= static_cast<int64_t>(size);
        }

        if (dataView.find(kCRLF) == 0) {
            dataView = dataView.substr(kCRLF.size());
        }
    }

    if (dataView.empty()) {
        data->reset();
    } else {
        data = std::make_unique<Data>(dataView.view());
    }
    return res;
}

bool Request::isReceivable() noexcept {
    while (true) {
        if (!isValid_) {
            disconnected();
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
        handleErrorResponse(canReceive.resultCode, canReceive.errorCode);
        return false;//disconnect
    }
}

bool Request::parseChunk(DataPtr& data, int64_t& chunkSize, bool& isCompleted) noexcept {
    return parseChunkHandler(data, chunkSize, isCompleted,
        [this](ResultCode resultCode, int32_t errorCode){
            handleErrorResponse(resultCode, errorCode);
        },
        [this](DataPtr data) {
            responseData(std::move(data));
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
            disconnected();
            return;
        }
        auto [recvResult, dataPtr] = std::move(socket_->receive());
        bool isCompleted = (recvResult.resultCode == ResultCode::Completed ||
                            recvResult.resultCode == ResultCode::Disconnected);
        if (!recvResult.isSuccess()) {
            if (recvResult.resultCode == ResultCode::Retry) {
                continue;
            }
            if (isCompleted) {
                isCompleted_ = true;
                disconnected();
            } else {
                this->handleErrorResponse(recvResult.resultCode, recvResult.errorCode);
            }
            return;
        }

        if (!parseHeaderSuccess) {
            recvDataPtr->append(std::move(dataPtr));
            auto [isSuccess, headerSize] = parseResponseHeader(recvDataPtr->view(), response);
            parseHeaderSuccess = isSuccess;
            if (!isSuccess) {
                continue;
            }
            if (response.isNeedRedirect() && info_.isAllowRedirect) {
                redirect(response.headers["Location"]);
                return;
            }
            parseFieldValue(response.headers, "Content-Length", contentLength);
            parseFieldValue(response.headers, "Transfer-Encoding", transferCoding);
            responseHeader(std::move(response));
            dataPtr = recvDataPtr->copy(headerSize);
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
                responseData(std::move(dataPtr));
            }
        }

        if (isCompleted) {
            isCompleted_ = true;
            disconnected();
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

void Request::handleErrorResponse(ResultCode code, int32_t errorCode) noexcept {
    if (isValid_ && handler_.onError) {
        handler_.onError(reqId_ , {code, errorCode});
    }
    disconnected();
}

void Request::responseHeader(ResponseHeader&& header) noexcept {
    if (isValid_ && handler_.onParseHeaderDone) {
        handler_.onParseHeaderDone(reqId_, std::move(header));
    }
}

void Request::responseData(DataPtr dataPtr) noexcept {
    if (isValid_ && handler_.onData) {
        handler_.onData(reqId_, std::move(dataPtr));
    }
}


void Request::disconnected() noexcept {
    if (isValid_ && handler_.onDisconnected) {
        handler_.onDisconnected(reqId_);
        socket_.reset(); //release resource
    }
}


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
        worker_->detach();
    }
}

void RequestSession::close() noexcept {
    {
        std::lock_guard<std::mutex> lock(taskMutex_);
        requestTaskQueue_.clear();
    }
    {
        std::lock_guard<std::mutex> lock(mutex_);
        handler_.reset();
        isExited_ = true;
    }
    cond_.notify_all();
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
    socket_ = std::unique_ptr<ISocket, decltype(&freeSocket)>(socketPtr, freeSocket);
    auto addressInfoPtr = MakeAddressInfoPtr(addressInfo);
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
        LogI("current task url:{}", currentTask_->requestInfo->url);
        if (!host_ || (host_ && host_->host != currentTask_->host->host) ) {
            LogI("host change");
            host_ = std::move(currentTask_->host);
            socket_.reset();
        }
        if (socket_ == nullptr) {
            setupSocket();
        }
        if (!send()) {
            continue;
        }
        receive();
    }
}

bool RequestSession::send() noexcept {
    auto canSend = socket_->canSend(getRemainTime());
    if (!currentTask_ || !canSend.isSuccess()) {
        handleErrorResponse(canSend.resultCode, canSend.errorCode);
        return false;
    }
    std::this_thread::sleep_for(1ms);
    currentTask_->requestInfo->headers["Connection"] = "keep-alive";
    if (!currentTask_->host) {
        currentTask_->host = std::unique_ptr<Url, decltype(&freeUrl)>(new Url(currentTask_->requestInfo->url), freeUrl);
    }
    auto sendData = encode::htmlEncode(*currentTask_->requestInfo, *currentTask_->host);
    auto dataView = std::string_view(sendData);
    LogI("send data: {}", dataView);
    do {
        auto [sendResult, sendSize] = socket_->send(dataView);
        if (!sendResult.isSuccess()) {
            handleErrorResponse(sendResult.resultCode, sendResult.errorCode);
            return false;
        } else if (sendSize < dataView.size()) {
            dataView = dataView.substr(static_cast<uint64_t>(sendSize));
        } else {
            break;
        }
    } while (!dataView.empty());
    return true;
}

std::string RequestSession::request(std::unique_ptr<RequestInfo> requestInfo) noexcept {
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
    taskPtr->reqId = reqId;
    taskPtr->startStamp = Time::nowTimeStamp();
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
        if (!currentTask_->isValid_ || isExited_) {
            disconnected();
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
        handleErrorResponse(canReceive.resultCode, canReceive.errorCode);
        return false;//disconnect
    }
}

bool RequestSession::parseChunk(DataPtr& data, int64_t& chunkSize, bool& isCompleted) noexcept {
    return parseChunkHandler(data, chunkSize, isCompleted,
        [this](ResultCode resultCode, int32_t errorCode){
            handleErrorResponse(resultCode, errorCode);
        },
        [this](DataPtr data) {
            responseData(std::move(data));
        });
}

void RequestSession::redirect(const std::string& host) noexcept {
    LogI("redirect:{}", host);
    bool isSuccess = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        host_ = std::unique_ptr<Url, decltype(&freeUrl)>(new Url(host), freeUrl);
        if (!host_->isValid() || !host_->isSupportScheme()) {
            handleErrorResponse(ResultCode::RedirectError, 0);
        } else {
            isSuccess = true;
        }
    }
    if (isSuccess) {
        currentTask_->isRedirect_ = true;
    }
}

void RequestSession::receive() noexcept {
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
            disconnected();
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
                disconnected();
            } else {
                handleErrorResponse(recvResult.resultCode, recvResult.errorCode);
            }
            return;
        }

        if (!parseHeaderSuccess) {
            recvDataPtr->append(std::move(dataPtr));
            auto [isSuccess, headerSize] = parseResponseHeader(recvDataPtr->view(), response);
            parseHeaderSuccess = isSuccess;
            if (!isSuccess) {
                continue;
            }
            if (response.isNeedRedirect() && currentTask_->requestInfo->isAllowRedirect) {
                redirect(response.headers["Location"]);
                return;
            }
            parseFieldValue(response.headers, "Content-Length", contentLength);
            parseFieldValue(response.headers, "Transfer-Encoding", transferCoding);
            responseHeader(std::move(response));
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
                responseData(std::move(dataPtr));
            }
        }

        if (isCompleted) {
            currentTask_->isCompleted_ = true;
            disconnected();
            return; //disconnect
        }
    }
}

void RequestSession::responseHeader(ResponseHeader&& header) noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    if (currentTask_ && handler_ && handler_->onParseHeaderDone) {
        handler_->onParseHeaderDone(currentTask_->reqId, std::move(header));
    }
}

void RequestSession::responseData(DataPtr data) noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    if (currentTask_ && handler_ && handler_->onData) {
        handler_->onData(currentTask_->reqId, std::move(data));
    }
}

void RequestSession::handleErrorResponse(ResultCode code, int32_t errorCode) noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    if (currentTask_ && handler_ && handler_->onError) {
        handler_->onError(currentTask_->reqId, {code, errorCode});
    }
}

void RequestSession::disconnected() noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    if (currentTask_ && handler_ && handler_->onDisconnected) {
        handler_->onDisconnected(currentTask_->reqId);
    }
}

int64_t RequestSession::getRemainTime() noexcept {
    auto t = Time::nowTimeStamp() - Time::TimePoint(currentTask_->startStamp);
    auto diff = currentTask_->requestInfo->timeout - t.toMilliSeconds();
    return std::max<int64_t>(diff.count(), 0ll);
}

} //end of namespace slark::http
