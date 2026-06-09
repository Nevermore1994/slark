//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#include "HttpUtil.hpp"

#include "DataView.hpp"
#include "StringUtil.h"
#include "Url.h"

namespace slark::http::Util {

namespace {

constexpr std::string_view kMethodNames[] =
    {"Unknown", "GET", "POST", "PUT", "PATCH", "DELETE", "OPTIONS"};

constexpr std::string_view kCRLF = "\r\n"sv;
constexpr std::string_view kHeaderEnd = "\r\n\r\n"sv;
constexpr std::string_view kHTTPFlag = "HTTP/"sv;
constexpr std::string_view kBase64Chars =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/"sv;

// chunked body 解码
bool decodeChunkedBody(const void* data,
                       size_t bodyStart,
                       size_t totalSize,
                       DataPtr& outBody) noexcept {
    auto bodyData = std::make_unique<Data>(
        ConstByteSpan(reinterpret_cast<const Byte*>(data) + bodyStart, totalSize - bodyStart));

    // 预估容量：通常是原始数据的70-80%
    auto estimatedSize = static_cast<size_t>(static_cast<double>(bodyData->length()) * 0.75);
    std::vector<Byte> decoded;
    decoded.reserve(estimatedSize);

    ChunkParseContext ctx;
    ChunkParseCallbacks callbacks;
    bool hasError = false;

    callbacks.onError = [&hasError](ResultCode, int32_t) {
        hasError = true;
    };
    callbacks.onData = [&decoded](DataPtr chunk) {
        if (chunk) {
            auto view = chunk->view();
            decoded.insert(decoded.end(), view.begin(), view.end());
        }
    };

    if (!parseChunkData(bodyData, ctx, callbacks) || hasError) {
        return false;
    }

    if (!decoded.empty()) {
        outBody = std::make_unique<Data>(ConstByteSpan(decoded.data(), decoded.size()));
    }
    return true;
}

// Content-Length body 提取
bool extractFixedBody(const void* data,
                      size_t bodyStart,
                      size_t totalSize,
                      std::string_view contentLengthStr,
                      DataPtr& outBody) noexcept {
    auto available = totalSize - bodyStart;

    if (!contentLengthStr.empty()) {
        auto expectedLen = StringUtil::fromChars<uint64_t>(contentLengthStr);
        if (expectedLen) {
            if (available < *expectedLen) {
                return false;
            }
            available = *expectedLen;
        }
    }

    if (available > 0) {
        outBody = std::make_unique<Data>(
            ConstByteSpan(reinterpret_cast<const Byte*>(data) + bodyStart, available));
    }
    return true;
}

std::string getHeader(const std::unordered_map<std::string, std::string>& headers,
                      std::string_view key) {
    for (const auto& [k, v] : headers) {
        if (StringUtil::iequals(k, key)) {
            return v;
        }
    }
    return "";
}

} // anonymous namespace

AddressInfoPtr resolveAddress(const std::string& host,
                              const std::string& port,
                              IPVersion ipVersion) {
    struct addrinfo hints{};
    hints.ai_family = getAddressFamily(ipVersion);
    hints.ai_socktype = SOCK_STREAM;

    struct addrinfo* result = nullptr;
    int32_t ret = getaddrinfo(host.c_str(), port.c_str(), &hints, &result);
    if (ret != 0 || !result) {
        return makeAddressInfo(nullptr);
    }

    return makeAddressInfo(result);
}

AddressInfoPtr resolveAddress(const Url& url, IPVersion ipVersion) {
    auto port = url.port.empty() ? (url.isHttps() ? "443" : "80") : url.port;
    return resolveAddress(url.host, port, ipVersion);
}

std::string getMethodName(HttpMethodType type) {
    auto index = static_cast<size_t>(type);
    return std::string(index < std::size(kMethodNames) ? kMethodNames[index] : kMethodNames[0]);
}

std::string base64Encode(const std::string& str) {
    std::string result;
    result.reserve(((str.length() + 2) / 3) * 4);

    int32_t accumulator = 0;
    int32_t bits = -6;

    for (auto c : str) {
        accumulator = (accumulator << 8) + static_cast<uint8_t>(c);
        bits += 8;
        while (bits >= 0) {
            result.push_back(kBase64Chars[(accumulator >> bits) & 0x3F]);
            bits -= 6;
        }
    }

    if (bits > -6) {
        result.push_back(kBase64Chars[((accumulator << 8) >> (bits + 8)) & 0x3F]);
    }

    while (result.size() % 4) {
        result.push_back('=');
    }
    return result;
}

bool parseChunkData(DataPtr& data,
                    ChunkParseContext& context,
                    const ChunkParseCallbacks& callbacks) noexcept {
    DataView dataView(data->view());
    while (!dataView.empty()) {
        // 跳过chunk间的CRLF分隔符
        if (dataView.find(kCRLF) == 0) {
            dataView = dataView.subview(kCRLF.size());
            continue;
        }

        // 需要解析新的chunk大小
        if (context.chunkSize <= 0) {
            auto pos = dataView.find(kCRLF);
            if (pos == std::string_view::npos) {
                break; // 数据不完整，等待更多数据
            }

            auto sizeStr = dataView.subview(0, pos).toString();
            // chunk-size可能带有chunk-extension，取分号前的部分
            auto semicolonPos = sizeStr.find(';');
            if (semicolonPos != std::string::npos) {
                sizeStr = sizeStr.substr(0, semicolonPos);
            }

            auto chunkSizeResult = StringUtil::fromChars<int64_t>(sizeStr, 16);
            if (!chunkSizeResult) {
                callbacks.onError(ResultCode::ChunkSizeError, 0);
                return false;
            }
            context.chunkSize = *chunkSizeResult;

            dataView = dataView.subview(pos + kCRLF.size());

            // chunk大小为0表示传输结束
            if (context.chunkSize == 0) {
                context.isCompleted = true;
                break;
            }
            continue;
        }

        // 读取chunk数据
        auto readSize = std::min(static_cast<size_t>(context.chunkSize), dataView.size());
        callbacks.onData(std::make_unique<Data>(dataView.subview(0, readSize).span()));
        dataView = dataView.subview(readSize);
        context.chunkSize -= static_cast<int64_t>(readSize);
    }

    // 保留未处理的数据
    if (dataView.empty()) {
        data->reset();
    } else {
        data = std::make_unique<Data>(dataView.span());
    }

    return true;
}

std::tuple<bool, int64_t> parseResponseHeader(DataView data, ResponseHeader& response) noexcept {
    auto headerEndPos = data.find(kHeaderEnd);
    if (headerEndPos == std::string_view::npos) {
        return {false, 0};
    }

    auto headerView = data.substr(0, headerEndPos);
    auto lines = headerView.split(kCRLF);
    if (lines.empty()) {
        return {false, 0};
    }

    // 解析状态行: "HTTP/1.1 200 OK"
    auto statusLine = lines[0];
    if (statusLine.find(kHTTPFlag) == std::string_view::npos) {
        return {false, 0};
    }

    // 按空格分割状态行，至少需要3个部分
    auto firstSpace = statusLine.find(' ');
    if (firstSpace == std::string_view::npos) {
        return {false, 0};
    }

    auto version = statusLine.substr(0, firstSpace);
    auto afterVersion = statusLine.substr(firstSpace + 1);

    auto secondSpace = afterVersion.find(' ');
    if (secondSpace == std::string_view::npos) {
        return {false, 0};
    }

    auto statusCodeStr = afterVersion.substr(0, secondSpace);
    auto reasonPhrase = afterVersion.substr(secondSpace + 1);

    auto statusCodeResult = StringUtil::fromChars<int32_t>(statusCodeStr.toStringView());
    if (!statusCodeResult) {
        return {false, 0};
    }
    response.httpStatusCode = static_cast<HttpStatusCode>(*statusCodeResult);

    response.headers["Version"] = version.toString();
    response.reasonPhrase = reasonPhrase.toString();

    // 解析响应头: "Key: Value"
    for (size_t i = 1; i < lines.size(); ++i) {
        auto& line = lines[i];
        auto colonPos = line.find(':');
        if (colonPos == std::string_view::npos) {
            continue;
        }

        auto name = line.substr(0, colonPos);
        // value取冒号后的全部内容（处理value中包含冒号的情况）
        auto value = line.substr(colonPos + 1);
        response.headers[name.removePrefix(' ').toString()] = value.removePrefix(' ').toString();
    }

    return {true, headerEndPos + kHeaderEnd.size()};
}

std::string buildRequestString(const RequestInfo& info, const Url& url, bool keepAlive) noexcept {
    std::string result;
    result.reserve(256);

    result.append(getMethodName(info.methodType)).append(" ").append(url.path);
    if (!url.query.empty()) {
        result.append("?").append(url.query);
    }
    result.append(" HTTP/1.1\r\n");

    result.append("Host: ").append(url.host);
    if ((url.isHttp() && url.port != "80") || (url.isHttps() && url.port != "443")) {
        result.append(":").append(url.port);
    }
    result.append(kCRLF);
    for (const auto& [key, value] : info.headers) {
        // 过滤 CRLF，防止 header injection
        if (key.find('\r') != std::string::npos || key.find('\n') != std::string::npos ||
            value.find('\r') != std::string::npos || value.find('\n') != std::string::npos) {
            continue;
        }
        result.append(key).append(": ").append(value).append(kCRLF);
    }

    if (!info.bodyEmpty()) {
        result.append("Content-Length: ").append(std::to_string(info.bodySize())).append(kCRLF);
    }

    result.append("Connection: ").append(keepAlive ? "keep-alive" : "close").append(kCRLF);
    result.append(kCRLF);
    return result;
}

bool parseRawResponse(const void* data, size_t size, ParsedResponse& out) noexcept {
    auto dataView = DataView(reinterpret_cast<const Byte*>(data), size);
    ResponseHeader header;
    auto [success, bodyOffset] = parseResponseHeader(dataView, header);
    if (!success) {
        return false;
    }

    out.statusCode = header.httpStatusCode;
    out.headers = std::move(header.headers);
    out.reasonPhrase = std::move(header.reasonPhrase);

    auto connValue = getHeader(out.headers, "Connection");
    out.shouldCloseConnection = (!connValue.empty() && StringUtil::toLower(connValue) == "close");

    auto bodyStart = static_cast<size_t>(bodyOffset);
    if (bodyStart >= size) {
        return true;
    }

    auto transferEncoding = getHeader(out.headers, "Transfer-Encoding");
    if (StringUtil::toLower(transferEncoding).find("chunked") != std::string::npos) {
        return decodeChunkedBody(data, bodyStart, size, out.body);
    }

    return extractFixedBody(data,
                            bodyStart,
                            size,
                            getHeader(out.headers, "Content-Length"),
                            out.body);
}

} // namespace slark::http::Util