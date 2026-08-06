//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//
#pragma once

#include "Type.h"
#include "Url.h"

namespace slark::http::Util {

std::string getMethodName(HttpMethodType type);

/// Validate request method and framing-sensitive headers before opening a connection.
bool validateRequest(const RequestInfo& info, const Url& url) noexcept;

/// Build an HTTP request string (request line + headers)
std::string buildRequestString(const RequestInfo& info,
                               const Url& url,
                               bool keepAlive = false) noexcept;

}  // namespace slark::http::Util
