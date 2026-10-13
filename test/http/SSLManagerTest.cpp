//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#include "SSLManager.h"

#include <gtest/gtest.h>

#include <openssl/ssl.h>

#include "Socket.hpp"

using namespace slark;
using namespace slark::http;

#if ENABLE_HTTPS

TEST(SSLManagerTest, MinProtoVersionIsAtLeastTls12) {
    auto& context = SSLManager::shareContext();
    ASSERT_NE(context, nullptr);
    const auto version = SSL_CTX_get_min_proto_version(context.get());
    EXPECT_GE(version, static_cast<long>(TLS1_2_VERSION));
}

TEST(SSLManagerTest, CreateForIpLiteralProducesSsl) {
    auto ssl = SSLManager::create(kInvalidSocket, "10.0.0.1");
    EXPECT_NE(ssl, nullptr);

    auto byName = SSLManager::create(kInvalidSocket, "example.com");
    EXPECT_NE(byName, nullptr);

    auto empty = SSLManager::create(kInvalidSocket, "");
    EXPECT_NE(empty, nullptr);
}

TEST(SocketUtilTest, IsIpLiteral) {
    EXPECT_TRUE(isIpLiteral("10.0.0.1"));
    EXPECT_TRUE(isIpLiteral("::1"));
    EXPECT_TRUE(isIpLiteral("2001:db8::1"));
    EXPECT_FALSE(isIpLiteral("example.com"));
    EXPECT_FALSE(isIpLiteral(""));
    EXPECT_FALSE(isIpLiteral("999.1.1.1"));
}

#endif
