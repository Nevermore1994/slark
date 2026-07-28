//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#pragma once

#include <chrono>
#include <list>
#include <mutex>
#include <string>
#include <unordered_map>

#include "Awaitable.hpp"
#include "Socket.hpp"
#include "Task.hpp"
#include "ThreadPool.hpp"

namespace slark::http {

struct DnsCacheEntry {
    AddressInfoPtr address;
    std::chrono::steady_clock::time_point expireAt;
};

/// 带 LRU 缓存的异步 DNS 解析器（单例）
class DnsResolver {
 public:
    struct Config {
        uint32_t threadCount;
        size_t maxCacheSize;
        std::chrono::seconds cacheTtl;

        Config() : threadCount(2), maxCacheSize(256), cacheTtl(60) {}
    };

    static DnsResolver& instance() noexcept;

    explicit DnsResolver(const Config& config = Config{});
    ~DnsResolver() = default;

    /// 异步解析，优先命中缓存；缓存未命中时在线程池中执行 getaddrinfo
    Task<AddressInfoPtr> resolveAsync(const std::string& host,
                                      const std::string& port,
                                      IPVersion ipVersion);

    /// 主动清除指定 host 的缓存（如连接失败时调用）
    void invalidate(const std::string& host) noexcept;

    /// 清空全部缓存
    void clearCache() noexcept;

 private:
    std::string makeKey(const std::string& host,
                        const std::string& port,
                        IPVersion ipVersion) const;

    AddressInfoPtr lookupCache(const std::string& key) noexcept;
    void insertCache(const std::string& key, const AddressInfoPtr& addr);
    static AddressInfoPtr cloneAddrInfo(const addrinfo* src) noexcept;

    Config config_;
    ThreadPool threadPool_;

    mutable std::mutex cacheMutex_;
    std::list<std::string> lruList_;

    struct CacheValue {
        DnsCacheEntry entry;
        std::list<std::string>::iterator lruIt;
    };

    std::unordered_map<std::string, CacheValue> cache_;
};

}  // namespace slark::http
