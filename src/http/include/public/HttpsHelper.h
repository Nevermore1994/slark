//
// Created by Nevermore on 2024/7/6.
// slark HttpsHelper
// Copyright (c) 2024 Nevermore All rights reserved.
//
#if ENABLE_HTTPS
#pragma once
#include "Base.h"
DISABLE_WARNINGS
#include "openssl/ssl.h"
ENABLE_WARNINGS
#include <functional>

namespace slark::http {

using SSLPtr = std::unique_ptr<SSL, decltype(&SSL_free)>;
using SSLContextPtr = std::unique_ptr<SSL_CTX, decltype(&SSL_CTX_free)>;

class HttpsHelper {
public:
    static std::function<void(SSLContextPtr&)> configContext;
};

} //end of namespace http
#endif //end ENABLE_HTTPS
