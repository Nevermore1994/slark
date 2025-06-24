//
// Created by Nevermore- on 2025/6/23.
//

#include <algorithm>
#include "include/Url.h"
#include "include/HttpUtil.h"

namespace slark::http::Util {

constexpr const char* kMethodNameArray[] = {"Unknown", "GET", "POST", "PUT", "PATCH", "DELETE", "OPTIONS"};

std::string getMethodName(HttpMethodType type) {
    return kMethodNameArray[static_cast<int>(type)];
}

///https://stackoverflow.com/questions/180947/base64-pushFrameDecode-snippet-in-c
constexpr std::string_view kBase64Content = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/"sv;

std::string base64Encode(const std::string& str) {
    std::string res;
    res.reserve(str.length());
    int val = 0, valb = -6;
    for (const auto& c: str) {
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

std::string htmlEncode(
    RequestInfo& info,
    const Url& url
) noexcept {
    auto& headers = info.headers;
    if (!info.bodyEmpty()) {
        headers["Content-Length"] = std::to_string(info.bodySize());
    }
    headers["Host"] = url.host;
    if (headers.count("Authorization") == 0 && !url.userInfo
        .empty()) {
        headers["Authorization"] = std::string("Basic ") + base64Encode(url.userInfo);
    }
    std::ostringstream oss;
    oss << getMethodName(info.methodType) << " " << url.path << (url.query.empty() ? "" : "?" + url.query)
        << " HTTP/1.1\r\n";
    std::for_each(
        headers.begin(),
        headers.end(),
        [&](const auto& pair) {
            oss << pair.first << ": " << pair.second << "\r\n";
        }
    );
    if (!info.bodyEmpty()) {
        oss << "\r\n";
        oss << info.body->view();
    }
    oss << "\r\n";
    return oss.str();
}

bool parseChunkHandler(
    DataPtr& data,
    int64_t& chunkSize,
    bool& isCompleted,
    const std::function<void(ResultCode, int32_t)>& errorHandler,
    const std::function<void(DataPtr)>& dataHandler
) noexcept {
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

std::tuple<bool, int64_t> parseResponseHeader(
    DataView data,
    ResponseHeader& response
) noexcept {
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

} // slark::http::Util