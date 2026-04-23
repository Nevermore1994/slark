//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#pragma once

#include <functional>
#include <memory>

using SSL_CTX = struct ssl_ctx_st;
using SSL = struct ssl_st;

namespace slark::http {

using SSLContextPtr = std::unique_ptr<SSL_CTX, void (*)(SSL_CTX*)>;
using SSLPtr = std::unique_ptr<SSL, void (*)(SSL*)>;

struct HttpsHelper {
    static std::function<void(SSLContextPtr&)> configContext;
};

} // namespace slark::http
