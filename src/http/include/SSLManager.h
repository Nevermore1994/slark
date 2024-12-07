//
// Created by Nevermore on 2024/6/18.
// slark SSLManager
// Copyright (c) 2024 Nevermore All rights reserved.
//
#pragma once

#if ENABLE_HTTPS
#include <mutex>
#include <openssl/err.h>

#include "PlainSocket.h"
#include "HttpsHelper.h"

namespace slark::http {

class SSLManager {
public:
    static SSLContextPtr& shareContext();

    static SSLPtr create(Socket) noexcept;
    static SocketResult connect(SSLPtr&) noexcept;
    [[nodiscard]] static std::tuple<SocketResult, int64_t> write(const SSLPtr&, const std::string_view&) noexcept;
    [[nodiscard]] static std::tuple<SocketResult, DataPtr> read(const SSLPtr&) noexcept;
    static void close(SSLPtr&) noexcept;
private:
    SSLManager();
    ~SSLManager();
};

} //end of namespace slark::http

#endif
