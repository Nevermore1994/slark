//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#pragma once

#if ENABLE_HTTPS

#include "Base.h"

DISABLE_WARNINGS
#include "openssl/err.h"
ENABLE_WARNINGS

#include <mutex>

#include "HttpsHelper.h"
#include "Socket.hpp"

namespace slark::http {

class SSLManager {
 public:
    static SSLContextPtr& shareContext();
    static SSLPtr create(Socket socket, const std::string& hostname) noexcept;

 private:
    SSLManager();
    ~SSLManager();
};

}  // namespace slark::http

#endif
