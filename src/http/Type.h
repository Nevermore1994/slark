//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#pragma once

#include "Data.h"
#include "Error.h"

#include <chrono>
#include <cstdint>
#include <expected>
#include <functional>
#include <string>
#include <unordered_map>

namespace slark::http {

constexpr int32_t kInvalid = -1;
constexpr int32_t kMaxRetryCount = 64;

#ifdef __clang__
#pragma clang diagnostic push
#pragma ide diagnostic ignored "OCUnusedGlobalDeclarationInspection"
#endif

enum class IPVersion : uint8_t {
    V4,
    V6,
    Auto,
};

enum class HttpMethodType : uint8_t {
    Unknown = 0,
    Get = 1,
    Post = 2,
    Put = 3,
    Patch = 4,
    Delete = 5,
    Options = 6,
    Head = 7,
};

enum class HttpStatusCode : uint16_t {
    Unknown,

    // 1xx - Informational
    Continue = 100,
    SwitchingProtocols = 101,
    Processing = 102,
    EarlyHints = 103,

    // 2xx - Successful
    OK = 200,
    Created = 201,
    Accepted = 202,
    NonAuthoritativeInformation = 203,
    NoContent = 204,
    ResetContent = 205,
    PartialContent = 206,
    MultiStatus = 207,
    AlreadyReported = 208,
    IMUsed = 226,

    // 3xx - Redirection
    MultipleChoices = 300,
    MovedPermanently = 301,
    Found = 302,
    SeeOther = 303,
    NotModified = 304,
    UseProxy = 305,
    TemporaryRedirect = 307,
    PermanentRedirect = 308,

    // 4xx - Client Error
    BadRequest = 400,
    Unauthorized = 401,
    PaymentRequired = 402,
    Forbidden = 403,
    NotFound = 404,
    MethodNotAllowed = 405,
    NotAcceptable = 406,
    ProxyAuthenticationRequired = 407,
    RequestTimeout = 408,
    Conflict = 409,
    Gone = 410,
    LengthRequired = 411,
    PreconditionFailed = 412,
    ContentTooLarge = 413,
    PayloadTooLarge = 413,
    URITooLong = 414,
    UnsupportedMediaType = 415,
    RangeNotSatisfiable = 416,
    ExpectationFailed = 417,
    ImATeapot = 418,
    MisdirectedRequest = 421,
    UnprocessableContent = 422,
    UnprocessableEntity = 422,
    Locked = 423,
    FailedDependency = 424,
    TooEarly = 425,
    UpgradeRequired = 426,
    PreconditionRequired = 428,
    TooManyRequests = 429,
    RequestHeaderFieldsTooLarge = 431,
    UnavailableForLegalReasons = 451,

    // 5xx - Server Error
    InternalServerError = 500,
    NotImplemented = 501,
    BadGateway = 502,
    ServiceUnavailable = 503,
    GatewayTimeout = 504,
    HTTPVersionNotSupported = 505,
    VariantAlsoNegotiates = 506,
    InsufficientStorage = 507,
    LoopDetected = 508,
    NotExtended = 510,
    NetworkAuthenticationRequired = 511,
};

// 超时来源类型
enum class TimeoutSource : uint8_t {
    None,
    Connect,
    TlsHandshake,
    Write,
    ReadIdle,
    ReadOverall,
};

// 连接池性能统计
struct ConnectionPoolPerf {
    bool reuseAttempted = false;
    bool reuseSucceeded = false;
    int64_t connectTimeMs = 0;
    int64_t tlsHandshakeTimeMs = 0;
    Error connectResult = Error::Success;
};

// 传输层性能统计
struct TransportPerf {
    int32_t sendRetryCount = 0;
    int32_t receiveWaitCount = 0;
    TimeoutSource timeoutSource = TimeoutSource::None;
    int64_t totalSendTimeMs = 0;
    int64_t totalReceiveTimeMs = 0;
};

// 解析层性能统计
struct ParserPerf {
    int64_t headerParseTimeUs = 0;
    int32_t chunkCount = 0;
    Error parseResult = Error::Success;
    int64_t bodySize = 0;
};

// 请求级性能统计
struct RequestPerf {
    ConnectionPoolPerf connection;
    TransportPerf transport;
    ParserPerf parser;
    int64_t totalRequestTimeMs = 0;
    std::string host;

    // 重置统计信息
    void reset() noexcept {
        connection = ConnectionPoolPerf{};
        transport = TransportPerf{};
        parser = ParserPerf{};
        totalRequestTimeMs = 0;
        host.clear();
    }

    // 获取摘要信息
    [[nodiscard]] std::string summary() const;
};

#ifdef __clang__
#pragma clang diagnostic pop
#endif

struct RequestInfo {
    bool isAllowRedirect = true;
    IPVersion ipVersion = IPVersion::Auto;
    std::string url;
    HttpMethodType methodType = HttpMethodType::Unknown;
    std::unordered_map<std::string, std::string> headers = {{"User-Agent", "(KHTML, like Gecko)"},
                                                            {"Accept", "*/*"}};
    DataRefPtr body = nullptr;
    std::chrono::milliseconds connectTimeout{std::chrono::seconds(10)};
    std::chrono::milliseconds tlsHandshakeTimeout{std::chrono::seconds(10)};
    std::chrono::milliseconds readTimeout{std::chrono::seconds(30)};
    std::chrono::milliseconds readIdleTimeout{std::chrono::seconds(30)};
    std::chrono::milliseconds writeTimeout{std::chrono::seconds(30)};
    std::string reqId;
    std::string tag;
    RequestPerf perf;

    [[nodiscard]] inline uint64_t bodySize() const noexcept {
        return body ? body->length() : 0;
    }

    [[nodiscard]] inline bool bodyEmpty() const noexcept {
        return !body || body->empty();
    }
};

using RequestInfoPtr = std::unique_ptr<RequestInfo>;

struct ErrorInfo {
    slark::Error retCode = slark::Error::Success;
    int32_t originErrorCode = 0;

    ErrorInfo() = default;

    explicit ErrorInfo(slark::Error error) : retCode(error) {}

    ErrorInfo(slark::Error error, int32_t origin)
        : retCode(error), originErrorCode(origin) {}
};

struct ResponseHeader {
    std::unordered_map<std::string, std::string> headers;
    HttpStatusCode httpStatusCode = HttpStatusCode::Unknown;
    std::string reasonPhrase;

    [[nodiscard]] bool isNeedRedirect() const noexcept {
        return httpStatusCode == HttpStatusCode::MovedPermanently ||  // 301
               httpStatusCode == HttpStatusCode::Found ||             // 302
               httpStatusCode == HttpStatusCode::SeeOther ||          // 303
               httpStatusCode == HttpStatusCode::TemporaryRedirect || // 307
               httpStatusCode == HttpStatusCode::PermanentRedirect;   // 308
    }

    ResponseHeader() = default;
};

struct HttpResponse {
    HttpStatusCode statusCode = HttpStatusCode::Unknown;
    std::unordered_map<std::string, std::string> headers;
    DataPtr body;
    std::string reasonPhrase;
    ErrorInfo error;
    RequestPerf perf;

    [[nodiscard]] bool isSuccess() const noexcept {
        return error.retCode == slark::Error::Success;
    }

    [[nodiscard]] bool isOk() const noexcept {
        return statusCode == HttpStatusCode::OK;
    }

    [[nodiscard]] bool isNeedRedirect() const noexcept {
        return statusCode == HttpStatusCode::MovedPermanently ||  // 301
               statusCode == HttpStatusCode::Found ||             // 302
               statusCode == HttpStatusCode::SeeOther ||          // 303
               statusCode == HttpStatusCode::TemporaryRedirect || // 307
               statusCode == HttpStatusCode::PermanentRedirect;   // 308
    }
};

// 流式响应块：用于 Generator 流式返回
struct StreamChunk {
    DataPtr data;                                         // 数据块内容
    bool isFinal = false;                                 // 是否为最后一个块（响应结束）
    ErrorInfo error;                                      // 错误信息（仅在出错时有效）
    HttpStatusCode statusCode = HttpStatusCode::Unknown;  // HTTP 状态码（首个块携带）
    std::unordered_map<std::string, std::string> headers; // 响应头（首个块携带）

    [[nodiscard]] bool hasError() const noexcept {
        return error.retCode != slark::Error::Success;
    }

    [[nodiscard]] bool hasData() const noexcept {
        return data && !data->empty();
    }

    static StreamChunk makeData(DataPtr d) {
        return StreamChunk{.data = std::move(d), .isFinal = false, .error = {}, .headers = {}};
    }

    static StreamChunk makeFinal() {
        return StreamChunk{.data = nullptr, .isFinal = true, .error = {}, .headers = {}};
    }

    static StreamChunk makeError(ErrorInfo err) {
        return StreamChunk{.data = nullptr,
                           .isFinal = true,
                           .error = std::move(err),
                           .statusCode = HttpStatusCode::Unknown,
                           .headers = {}};
    }

    static StreamChunk makeHeader(HttpStatusCode code,
                                  std::unordered_map<std::string, std::string> hdrs) {
        return StreamChunk{.data = nullptr,
                           .isFinal = false,
                           .error = {},
                           .statusCode = code,
                           .headers = std::move(hdrs)};
    }
};

struct HttpResponseHandler {
    std::function<void(const RequestInfo&)> onConnected;
    std::function<void(const RequestInfo&, ResponseHeader&&)> onHeader;
    std::function<void(const RequestInfo&, DataPtr)> onData;
    std::function<void(const RequestInfo&)> onCompleted;
    std::function<void(const RequestInfo&, ErrorInfo)> onError;
};

using ResponseResult = std::expected<HttpResponse, ErrorInfo>;

} // namespace slark::http
