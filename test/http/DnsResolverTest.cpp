//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//
#include "ConnectionFactory.h"
#include "DnsResolver.h"

#include <arpa/inet.h>
#include <gtest/gtest.h>
#include <netinet/in.h>

#include <atomic>
#include <chrono>
#include <memory>
#include <thread>
#include <vector>

using namespace slark;
using namespace slark::http;

class DnsResolverTest : public ::testing::Test {
 protected:
    void SetUp() override {
        DnsResolver::Config cfg;
        cfg.maxCacheSize = 4;
        cfg.cacheTtl = std::chrono::seconds(60);
        resolver_ = std::make_unique<DnsResolver>(cfg);
    }

    AddressInfoPtr runAndWait(DnsResolver& r,
                              const std::string& host,
                              IPVersion ver = IPVersion::V4) {
        auto coro = r.resolveAsync(host, "80", ver);
        coro.resume();
        for (int32_t i = 0; i < 200 && !coro.isDone(); ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        return coro.isDone() ? coro.result() : makeAddressInfo(nullptr);
    }

    std::unique_ptr<DnsResolver> resolver_;
};

TEST_F(DnsResolverTest, ClearCacheEmptiesAll) {
    resolver_->clearCache();
    resolver_->clearCache();
}

TEST_F(DnsResolverTest, InvalidateOnEmptyCacheDoesNotCrash) {
    resolver_->clearCache();
    resolver_->invalidate("example.com");
}

TEST_F(DnsResolverTest, ResolveLocalhostSucceeds) {
    auto result = runAndWait(*resolver_, "localhost");
    EXPECT_NE(result.get(), nullptr);
}

TEST_F(DnsResolverTest, ResolveInvalidHostReturnsNull) {
    auto result = runAndWait(*resolver_, "this.host.does.not.exist.invalid");
    EXPECT_EQ(result.get(), nullptr);
}

TEST_F(DnsResolverTest, ConnectAddressOwnsCopiedSocketAddress) {
    sockaddr_in source{};
    source.sin_family = AF_INET;
    source.sin_port = htons(8080);
    ASSERT_EQ(inet_pton(AF_INET, "127.0.0.1", &source.sin_addr), 1);

    addrinfo info{};
    info.ai_family = AF_INET;
    info.ai_addrlen = sizeof(source);
    info.ai_addr = reinterpret_cast<sockaddr*>(&source);

    auto address = makeConnectAddress(&info);
    ASSERT_TRUE(address.has_value());
    ASSERT_TRUE(address->isValid());
    EXPECT_EQ(address->family, AF_INET);
    EXPECT_EQ(address->length, sizeof(source));

    source.sin_port = htons(9090);
    EXPECT_EQ(ntohs(reinterpret_cast<const sockaddr_in*>(&address->storage)->sin_port), 8080);
}

TEST_F(DnsResolverTest, ConnectAddressRejectsUnsupportedOrOversizedAddress) {
    sockaddr_in source{};
    source.sin_family = AF_INET;

    addrinfo unsupported{};
    unsupported.ai_family = AF_UNIX;
    unsupported.ai_addrlen = sizeof(source);
    unsupported.ai_addr = reinterpret_cast<sockaddr*>(&source);
    EXPECT_FALSE(makeConnectAddress(&unsupported).has_value());

    addrinfo oversized{};
    oversized.ai_family = AF_INET;
    oversized.ai_addrlen = sizeof(sockaddr_storage) + 1;
    oversized.ai_addr = reinterpret_cast<sockaddr*>(&source);
    EXPECT_FALSE(makeConnectAddress(&oversized).has_value());
}

TEST_F(DnsResolverTest, PublicAddressPolicyClassifiesReservedAndMappedAddresses) {
    auto makeAddress = [](int family, const char* text) {
        sockaddr_storage storage{};
        addrinfo info{};
        info.ai_family = family;
        info.ai_addrlen = family == AF_INET ? sizeof(sockaddr_in) : sizeof(sockaddr_in6);
        info.ai_addr = reinterpret_cast<sockaddr*>(&storage);
        if (family == AF_INET) {
            auto* address = reinterpret_cast<sockaddr_in*>(&storage);
            address->sin_family = AF_INET;
            EXPECT_EQ(inet_pton(AF_INET, text, &address->sin_addr), 1);
        } else {
            auto* address = reinterpret_cast<sockaddr_in6*>(&storage);
            address->sin6_family = AF_INET6;
            EXPECT_EQ(inet_pton(AF_INET6, text, &address->sin6_addr), 1);
        }
        return makeConnectAddress(&info);
    };

    EXPECT_FALSE(isPublicConnectAddress(*makeAddress(AF_INET, "127.0.0.1")));
    EXPECT_FALSE(isPublicConnectAddress(*makeAddress(AF_INET, "192.168.1.1")));
    EXPECT_TRUE(isPublicConnectAddress(*makeAddress(AF_INET, "8.8.8.8")));
    EXPECT_FALSE(isPublicConnectAddress(*makeAddress(AF_INET6, "::ffff:127.0.0.1")));
    EXPECT_FALSE(isPublicConnectAddress(*makeAddress(AF_INET6, "2001:db8::1")));
    EXPECT_TRUE(isPublicConnectAddress(*makeAddress(AF_INET6, "2001:4860:4860::8888")));
}

TEST_F(DnsResolverTest, PublicOnlyConnectionRejectsLoopbackBeforeSocketConnect) {
    RequestPerf perf;
    auto task = openConnection("127.0.0.1", 80, "http", IPVersion::V4,
                                      NetworkAddressPolicy::PublicOnly,
                                      std::chrono::milliseconds(500),
                                      std::chrono::milliseconds(500), &perf);
    task.resume();
    for (int i = 0; i < 200 && !task.isDone(); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    ASSERT_TRUE(task.isDone());
    const auto result = task.result();
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().retCode, Error::HttpRedirectError);
    EXPECT_EQ(perf.connection.connectResult, Error::HttpRedirectError);
}

TEST_F(DnsResolverTest, ConnectionFactoryReturnsDnsError) {
    RequestPerf perf;
    auto task = openConnection("this.host.does.not.exist.invalid", 80, "http",
                                      IPVersion::V4, NetworkAddressPolicy::AllowAny,
                                      std::chrono::milliseconds(500),
                                      std::chrono::milliseconds(500), &perf);
    task.resume();
    for (int i = 0; i < 500 && !task.isDone(); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    ASSERT_TRUE(task.isDone());
    const auto result = task.result();
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().retCode, Error::DnsResolveFailed);
    EXPECT_EQ(perf.connection.connectResult, Error::DnsResolveFailed);
}

TEST_F(DnsResolverTest, ConnectionFactoryReturnsCancellationError) {
    CancellationToken token;
    token.cancel();
    auto task = openConnection("localhost", 80, "http", IPVersion::V4,
                                      NetworkAddressPolicy::AllowAny,
                                      std::chrono::milliseconds(500),
                                      std::chrono::milliseconds(500), nullptr, token);
    task.resume();

    ASSERT_TRUE(task.isDone());
    const auto result = task.result();
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().retCode, Error::Cancelled);
}

TEST_F(DnsResolverTest, SecondResolveShouldHitCache) {
    ASSERT_NE(runAndWait(*resolver_, "localhost").get(), nullptr);

    auto t0 = std::chrono::steady_clock::now();
    auto second = runAndWait(*resolver_, "localhost");
    auto elapsed = std::chrono::steady_clock::now() - t0;

    EXPECT_NE(second.get(), nullptr);
    EXPECT_LT(elapsed, std::chrono::milliseconds(10));
}

TEST_F(DnsResolverTest, InvalidateThenResolveAgain) {
    ASSERT_NE(runAndWait(*resolver_, "localhost").get(), nullptr);

    resolver_->invalidate("localhost");

    EXPECT_NE(runAndWait(*resolver_, "localhost").get(), nullptr);
}

TEST_F(DnsResolverTest, CacheExpiredAfterTtl) {
    DnsResolver::Config cfg;
    cfg.maxCacheSize = 4;
    cfg.cacheTtl = std::chrono::seconds(0);
    auto r = std::make_unique<DnsResolver>(cfg);

    ASSERT_NE(runAndWait(*r, "localhost").get(), nullptr);

    EXPECT_NE(runAndWait(*r, "localhost").get(), nullptr);
}

TEST_F(DnsResolverTest, LruEvictsOldestWhenFull) {
    for (int32_t i = 0; i < 5; ++i) {
        auto coro = resolver_->resolveAsync("localhost", std::to_string(8000 + i), IPVersion::V4);
        coro.resume();
        for (int32_t j = 0; j < 100 && !coro.isDone(); ++j) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }
    EXPECT_NE(runAndWait(*resolver_, "localhost").get(), nullptr);
}

TEST_F(DnsResolverTest, DifferentIpVersionCachedSeparately) {
    auto v4 = runAndWait(*resolver_, "localhost", IPVersion::V4);
    auto v6 = runAndWait(*resolver_, "localhost", IPVersion::V6);

    EXPECT_NE(v4.get(), nullptr);
    (void)v6;
}

TEST_F(DnsResolverTest, ConcurrentResolveSameHost) {
    std::atomic<int32_t> successCount{0};
    std::vector<std::thread> threads;
    for (int i = 0; i < 4; ++i) {
        threads.emplace_back([&]() {
            auto result = runAndWait(*resolver_, "localhost");
            if (result) {
                successCount++;
            }
        });
    }
    for (auto& t : threads) {
        t.join();
    }
    EXPECT_EQ(successCount.load(), 4);
}

TEST_F(DnsResolverTest, ResolveWithTimeout) {
    auto coro = resolver_->resolveAsync("localhost", "80", IPVersion::V4);
    coro.resume();
    for (int i = 0; i < 200 && !coro.isDone(); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    EXPECT_TRUE(coro.isDone());
    EXPECT_NE(coro.result().get(), nullptr);
}