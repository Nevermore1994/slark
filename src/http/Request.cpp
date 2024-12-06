//
// Created by Nevermore on 2024/5/16.
// http-request Request
// Copyright (c) 2024 Nevermore All rights reserved.
//
#include <cstdint>
#include <string_view>
#include <utility>
#include <sstream>
#include <ios>
#include "Request.h"
#include "TSLSocket.h"
#include "Type.h"
#include "PlainSocket.h"
#include "Url.h"
#include "Random.hpp"
#include "StringUtil.h"
#include "Time.hpp"

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
    for (unsigned char c : str) {
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
    : info_(info)
    , handler_(responseHandler)
    , startStamp_(Time::nowTimeStamp())
    , reqId_(Random::randomString(20))
    , socket_(nullptr, freeSocket)
    , url_(nullptr, freeUrl) {
    config();
}

Request::Request(RequestInfo&& info, ResponseHandler&& responseHandler)
    : info_(std::move(info))
    , handler_(std::move(responseHandler))
    , startStamp_(Time::nowTimeStamp())
    , reqId_(Random::randomString(20)), socket_(nullptr,freeSocket)
    , url_(nullptr, freeUrl) {
    config();
}

Request::~Request() {
    if (worker_ && worker_->joinable()) {
        worker_->join();
    }
}

bool Request::init() {
#ifdef _WIN32
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        return false;
    }
#endif
    return true;
}

void Request::clear() {
#ifdef _WIN32
    WSACleanup();
#endif
}

void Request::config() noexcept {
    worker_ = std::make_unique<std::thread>(&Request::process, this);
}

#ifdef __clang__
#pragma clang diagnostic push
#pragma ide diagnostic ignored "misc-no-recursion"
#endif
#ifdef _WIN32
#pragma comment(lib, "Ws2_32.lib")
#endif //end _WIN32

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
    } else if (handler_.onConnected) {
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
    if (!url_->isValid() || !url_->isHttpScheme()) {
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
    if (!url_->isHttpScheme()) {
        handler(ResultCode::SchemeNotSupported);
        return;
    }
    sendRequest();
}

bool Request::send() noexcept {
    auto canSend = socket_->canSend(getRemainTime());
    if (!canSend.isSuccess()) {
        this->handleErrorResponse(canSend.resultCode, canSend.errorCode);
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

std::tuple<bool, int64_t> parseResponseHeader(std::string_view data, ResponseHeader& response) {
    constexpr std::string_view kCRLF = "\r\n"sv;
    constexpr std::string_view kHeaderEnd = "\r\n\r\n"sv;
    ///https://www.rfc-editor.org/rfc/rfc7230#section-3.1.2:~:text=header%2Dfield%20CRLF%20)-,CRLF,-%5B%20message%2Dbody%20%5D
    auto headerEndPos = data.find(kHeaderEnd);
    if (headerEndPos == std::string_view::npos) {
        return {false, 0};
    }
    auto headerView = data.substr(0, headerEndPos);

    auto headerViews = StringUtil::split(headerView, std::string_view(kCRLF));
    ///parse version and status
    auto statusView = headerViews[0];
    constexpr std::string_view kHTTPFlag = "HTTP/"sv;
    if (auto versionPos = statusView.find(kHTTPFlag); versionPos != std::string_view::npos) {
        ///HTTP-version SP status-code SP reason-phrase CRLF
        response.headers["Version"] = statusView.substr(versionPos, kHTTPFlag.size() + 3);
        response.httpStatusCode = static_cast<HttpStatusCode>(std::stoi(std::string(statusView.substr(kHTTPFlag.size() + 4, 3))));
        response.reasonPhrase = statusView.substr(versionPos + kHTTPFlag.size() + 3 + 1 + 3 + 1);
    }
    for (auto& view : headerViews) {
        if (view.empty() || view.find(':') == std::string_view::npos) {
            continue;
        }
        auto fieldValue = StringUtil::split(view, ": ");
        if (fieldValue.size() == 2) {
            auto name = std::string(fieldValue[0]);
            auto value = std::string(fieldValue[1]);
            response.headers[std::move(StringUtil::removePrefix(name, ' '))] = std::move(
            StringUtil::removePrefix(value, ' '));
        }
    }
    return {true, headerEndPos + kHeaderEnd.size()};
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
        this->handleErrorResponse(canReceive.resultCode, canReceive.errorCode);
        return false;//disconnect
    }
}

bool Request::parseChunk(DataPtr& data, int64_t& chunkSize, bool& isCompleted) noexcept {
    constexpr std::string_view kCRLF = "\r\n"sv;
    bool res = true;
    auto dataView = data->view();
    while (!dataView.empty()) {
        if (chunkSize <= 0) {
            auto pos = dataView.find(kCRLF);
            if (pos == std::string_view::npos) {
                this->handleErrorResponse(ResultCode::ChunkSizeError, 0);
                res = false;
                break;
            }
            chunkSize = std::stol(std::string(dataView.substr(0, pos)), nullptr, 16);
            if (chunkSize == 0) {
                isCompleted = true;
                break;
            }
            dataView = dataView.substr(pos + kCRLF.size());
        } else {
            auto size = std::min(static_cast<size_t>(chunkSize), dataView.size());
            responseData(std::make_unique<Data>(std::string(dataView.substr(0, size))));
            dataView = dataView.substr(size);
            chunkSize -= static_cast<int64_t>(size);
        }

        if (dataView.find(kCRLF) == 0) {
            dataView = dataView.substr(kCRLF.size());
        }
    }

    if (dataView.empty()) {
        data->destroy();
    } else {
        data = std::make_unique<Data>(std::string(dataView));
    }
    return res;
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
        auto [recvResult, dataPtr] = std::move(socket_->receive());
        bool isCompleted = (recvResult.resultCode == ResultCode::Completed ||
                            recvResult.resultCode == ResultCode::Disconnected);
        if (!recvResult.isSuccess()) {
            if (recvResult.resultCode == ResultCode::Retry) {
                continue;
            }
            if (isCompleted) {
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
            recvDataPtr->destroy();
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
    if (handler_.onError) {
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

} //end of namespace slark::http