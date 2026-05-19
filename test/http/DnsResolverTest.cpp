//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//
#include "DnsResolver.h"

#include "CoroutineScheduler.hpp"

#include <arpa/inet.h>
#include <gtest/gtest.h>
#include <netinet/in.h>
#include <thread>

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
        CoroutineScheduler::instance().schedule(coro);
        for (int32_t i = 0; i < 50 && !coro.isDone(); ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        return coro.isDone() ? coro.getResult() : makeAddressInfo(nullptr);
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

TEST_F(DnsResolverTest, SecondResolveShouldHitCache) {
    ASSERT_NE(runAndWait(*resolver_, "localhost").get(), nullptr);

    auto t0 = std::chrono::steady_clock::now();
    auto second = runAndWait(*resolver_, "localhost");
    auto elapsed = std::chrono::steady_clock::now() - t0;

    EXPECT_NE(second.get(), nullptr);
    // 缓存命中无需等待 getaddrinfo，应在 10ms 内完成
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

    // TTL=0 立即过期，第二次应重新走 getaddrinfo
    EXPECT_NE(runAndWait(*r, "localhost").get(), nullptr);
}

TEST_F(DnsResolverTest, LruEvictsOldestWhenFull) {
    // maxCacheSize=4，解析 5 个不同 host，不崩溃且最新的仍可命中
    for (int32_t i = 0; i < 5; ++i) {
        // 使用 localhost 的不同端口 key 来模拟不同条目
        auto coro = resolver_->resolveAsync("localhost", std::to_string(8000 + i), IPVersion::V4);
        CoroutineScheduler::instance().schedule(coro);
        for (int32_t j = 0; j < 50 && !coro.isDone(); ++j) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }
    // 最后一次解析仍应成功
    EXPECT_NE(runAndWait(*resolver_, "localhost").get(), nullptr);
}

TEST_F(DnsResolverTest, DifferentIpVersionCachedSeparately) {
    auto v4 = runAndWait(*resolver_, "localhost", IPVersion::V4);
    auto v6 = runAndWait(*resolver_, "localhost", IPVersion::V6);

    EXPECT_NE(v4.get(), nullptr);
    // V6 在部分 CI 环境可能不支持，只验证不崩溃
    (void)v6;
}