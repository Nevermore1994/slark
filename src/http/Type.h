//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#pragma once

#include <chrono>
#include <cstdint>
#include <expected>
#include <memory>
#include <string>
#include <unordered_map>

#include "Data.h"
#include "Error.h"

namespace slark::http {

class Http1Session;

constexpr int32_t kInvalid = -1;

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

enum class HttpConnectionMode : uint8_t {
    Close,
    KeepAlive,
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

// Timeout source type
enum class TimeoutSource : uint8_t {
    None,
    Connect,
    TlsHandshake,
    Write,
    ReadIdle,
    ReadOverall,
};

// Connection establishment performance stats
struct ConnectionPerf {
    int64_t connectTimeMs = 0;
    int64_t tlsHandshakeTimeMs = 0;
    Error connectResult = Error::Success;
};

// Transport layer performance stats
struct TransportPerf {
    int32_t sendRetryCount = 0;
    int32_t receiveWaitCount = 0;
    TimeoutSource timeoutSource = TimeoutSource::None;
    int64_t totalSendTimeMs = 0;
    int64_t totalReceiveTimeMs = 0;
};

// Parser layer performance stats
struct ParserPerf {
    int64_t headerParseTimeUs = 0;
    int32_t chunkCount = 0;
    Error parseResult = Error::Success;
    int64_t bodySize = 0;
};

// Request-level performance stats
struct RequestPerf {
    ConnectionPerf connection;
    TransportPerf transport;
    ParserPerf parser;
    int64_t totalRequestTimeMs = 0;
    std::string host;

};

#ifdef __clang__
#pragma clang diagnostic pop
#endif

constexpr uint64_t kDefaultMaxResponseBodySize = 64ULL * 1024ULL * 1024ULL;

enum class NetworkAddressPolicy : uint8_t {
    AllowAny,
    PublicOnly,
};

struct RequestInfo {
    bool isAllowRedirect = true;
    bool isRedirectTarget = false;
    NetworkAddressPolicy networkAddressPolicy = NetworkAddressPolicy::PublicOnly;
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
    HttpConnectionMode connectionMode = HttpConnectionMode::Close;
    std::shared_ptr<Http1Session> session;
    uint64_t maxResponseBodySize = kDefaultMaxResponseBodySize;
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

    ErrorInfo(slark::Error error, int32_t origin) : retCode(error), originErrorCode(origin) {}
};

struct ResponseHeader {
    std::unordered_map<std::string, std::string> headers;
    HttpStatusCode httpStatusCode = HttpStatusCode::Unknown;
    std::string reasonPhrase;

    ResponseHeader() = default;
};

enum class HttpResponseEventType : uint8_t {
    Connected,
    Headers,
    Data,
    Completed,
    Error,
};

struct HttpResponseEvent {
    HttpResponseEventType type = HttpResponseEventType::Error;
    ResponseHeader header;
    std::unordered_map<std::string, std::string> trailers;
    DataPtr data;
    RequestPerf perf;
    ErrorInfo error;

    static HttpResponseEvent makeConnected() {
        HttpResponseEvent event;
        event.type = HttpResponseEventType::Connected;
        return event;
    }

    static HttpResponseEvent makeHeaders(ResponseHeader responseHeader) {
        HttpResponseEvent event;
        event.type = HttpResponseEventType::Headers;
        event.header = std::move(responseHeader);
        return event;
    }

    static HttpResponseEvent makeData(DataPtr body) {
        HttpResponseEvent event;
        event.type = HttpResponseEventType::Data;
        event.data = std::move(body);
        return event;
    }

    static HttpResponseEvent makeCompleted(
        RequestPerf responsePerf,
        std::unordered_map<std::string, std::string> responseTrailers = {}) {
        HttpResponseEvent event;
        event.type = HttpResponseEventType::Completed;
        event.trailers = std::move(responseTrailers);
        event.perf = std::move(responsePerf);
        return event;
    }

    static HttpResponseEvent makeError(ErrorInfo responseError, RequestPerf responsePerf = {}) {
        HttpResponseEvent event;
        event.type = HttpResponseEventType::Error;
        event.error = std::move(responseError);
        event.perf = std::move(responsePerf);
        return event;
    }
};

struct HttpResponse {
    HttpStatusCode statusCode = HttpStatusCode::Unknown;
    std::unordered_map<std::string, std::string> headers;
    std::unordered_map<std::string, std::string> trailers;
    DataPtr body;
    std::string reasonPhrase;
    ErrorInfo error;
    RequestPerf perf;

    [[nodiscard]] bool hasValidResponse() const noexcept {
        const auto code = static_cast<uint16_t>(statusCode);
        return code >= 100 && code < 600;
    }

    [[nodiscard]] bool isTransportSuccess() const noexcept {
        return error.retCode == slark::Error::Success && hasValidResponse();
    }

    [[nodiscard]] bool isHttpSuccess() const noexcept {
        const auto code = static_cast<uint16_t>(statusCode);
        return isTransportSuccess() && code >= 200 && code < 300;
    }

    [[nodiscard]] bool isSuccess() const noexcept {
        return isTransportSuccess();
    }

    [[nodiscard]] bool isOk() const noexcept {
        return statusCode == HttpStatusCode::OK;
    }

    [[nodiscard]] bool isNeedRedirect() const noexcept {
        return statusCode == HttpStatusCode::MovedPermanently ||   // 301
               statusCode == HttpStatusCode::Found ||              // 302
               statusCode == HttpStatusCode::SeeOther ||           // 303
               statusCode == HttpStatusCode::TemporaryRedirect ||  // 307
               statusCode == HttpStatusCode::PermanentRedirect;    // 308
    }
};

// Streaming response chunk: used for Generator-based streaming returns
struct StreamChunk {
    DataPtr data;                                          // chunk data
    bool isFinal = false;                                  // whether this is the final chunk (end of response)
    ErrorInfo error;                                       // error info (only valid on error)
    HttpStatusCode statusCode = HttpStatusCode::Unknown;   // HTTP status code (carried by the first chunk)
    std::unordered_map<std::string, std::string> headers;  // response headers (carried by the first chunk)
    std::unordered_map<std::string, std::string> trailers; // final response metadata

    [[nodiscard]] bool hasError() const noexcept {
        return error.retCode != slark::Error::Success;
    }

    [[nodiscard]] bool hasData() const noexcept {
        return data && !data->empty();
    }

    static StreamChunk makeData(DataPtr d) {
        return StreamChunk{.data = std::move(d),
                           .isFinal = false,
                           .error = {},
                           .headers = {},
                           .trailers = {}};
    }

    static StreamChunk makeFinal(
        std::unordered_map<std::string, std::string> responseTrailers = {}) {
        return StreamChunk{.data = nullptr,
                           .isFinal = true,
                           .error = {},
                           .headers = {},
                           .trailers = std::move(responseTrailers)};
    }

    static StreamChunk makeError(ErrorInfo err) {
        return StreamChunk{.data = nullptr,
                           .isFinal = true,
                           .error = std::move(err),
                           .statusCode = HttpStatusCode::Unknown,
                           .headers = {},
                           .trailers = {}};
    }

    static StreamChunk makeHeader(HttpStatusCode code,
                                  std::unordered_map<std::string, std::string> hdrs) {
        return StreamChunk{.data = nullptr,
                           .isFinal = false,
                           .error = {},
                           .statusCode = code,
                           .headers = std::move(hdrs),
                           .trailers = {}};
    }
};

using ResponseResult = std::expected<HttpResponse, ErrorInfo>;

}  // namespace slark::http
