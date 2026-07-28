//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//
#include "DnsResolver.h"

#include <netdb.h>

#include "Log.hpp"

namespace slark::http {

DnsResolver& DnsResolver::instance() noexcept {
    static DnsResolver instance;
    return instance;
}

DnsResolver::DnsResolver(const Config& config)
    : config_(config), threadPool_(ThreadPoolConfig{config.threadCount}) {}

Task<AddressInfoPtr> DnsResolver::resolveAsync(const std::string& host,
                                               const std::string& port,
                                               IPVersion ipVersion) {
    auto key = makeKey(host, port, ipVersion);

    {
        std::lock_guard<std::mutex> lock(cacheMutex_);
        if (auto cached = lookupCache(key)) {
            LogI("[DnsResolver::resolveAsync] cache hit: {}:{} ipv={}",
                 host,
                 port,
                 static_cast<int>(ipVersion));
            co_return cached;
        }
    }

    LogI("[DnsResolver::resolveAsync] resolving: {}:{} ipv={}",
         host,
         port,
         static_cast<int>(ipVersion));

    auto awaiter = std::make_shared<CompletionAwaiter<AddressInfoPtr>>();

    auto future = threadPool_.submit([host, port, ipVersion, awaiter]() {
        struct addrinfo hints{};
        hints.ai_family = getAddressFamily(ipVersion);
        hints.ai_socktype = SOCK_STREAM;

        struct addrinfo* result = nullptr;
        int32_t ret = getaddrinfo(host.c_str(), port.c_str(), &hints, &result);
        if (ret != 0 || !result) {
            LogW("DNS resolve failed for {}:{} err={}", host, port, gai_strerror(ret));
            awaiter->complete(makeAddressInfo(nullptr));
            return;
        }
        awaiter->complete(makeAddressInfo(result));
    });

    if (!future.has_value()) {
        LogW("DNS thread pool submit failed for {}", host);
        co_return makeAddressInfo(nullptr);
    }

    // 挂起协程，等待 getaddrinfo 完成
    AddressInfoPtr resolved = co_await * awaiter;

    if (resolved) {
        std::lock_guard<std::mutex> lock(cacheMutex_);
        insertCache(key, resolved);
        LogI("[DnsResolver::resolveAsync] resolved & cached: {}:{}", host, port);
    } else {
        LogE("[DnsResolver::resolveAsync] resolve returned null: {}:{}", host, port);
    }

    co_return resolved;
}

void DnsResolver::invalidate(const std::string& host) noexcept {
    std::lock_guard<std::mutex> lock(cacheMutex_);
    auto it = cache_.begin();
    while (it != cache_.end()) {
        if (it->first.starts_with(host + ":")) {
            lruList_.erase(it->second.lruIt);
            it = cache_.erase(it);
        } else {
            ++it;
        }
    }
}

void DnsResolver::clearCache() noexcept {
    std::lock_guard<std::mutex> lock(cacheMutex_);
    cache_.clear();
    lruList_.clear();
}

std::string DnsResolver::makeKey(const std::string& host,
                                 const std::string& port,
                                 IPVersion ipVersion) const {
    return host + ":" + port + ":" + std::to_string(static_cast<uint8_t>(ipVersion));
}

AddressInfoPtr DnsResolver::lookupCache(const std::string& key) noexcept {
    auto it = cache_.find(key);
    if (it == cache_.end()) {
        return makeAddressInfo(nullptr);
    }

    auto& val = it->second;
    if (std::chrono::steady_clock::now() > val.entry.expireAt) {
        lruList_.erase(val.lruIt);
        cache_.erase(it);
        return makeAddressInfo(nullptr);
    }

    lruList_.splice(lruList_.begin(), lruList_, val.lruIt);
    return cloneAddrInfo(val.entry.address.get());
}

void DnsResolver::insertCache(const std::string& key, const AddressInfoPtr& addr) {
    if (auto it = cache_.find(key); it != cache_.end()) {
        lruList_.erase(it->second.lruIt);
        cache_.erase(it);
    }

    // 超出容量，淘汰 LRU 尾部
    if (cache_.size() >= config_.maxCacheSize) {
        auto& evictKey = lruList_.back();
        cache_.erase(evictKey);
        lruList_.pop_back();
    }

    lruList_.push_front(key);
    cache_.insert_or_assign(
        key,
        CacheValue{
            .entry = {.address = cloneAddrInfo(addr.get()),
                      .expireAt = std::chrono::steady_clock::now() + config_.cacheTtl},
            .lruIt = lruList_.begin(),
        });
}

AddressInfoPtr DnsResolver::cloneAddrInfo(const addrinfo* src) noexcept {
    if (!src) {
        return makeAddressInfo(nullptr);
    }

    auto* dst = new (std::nothrow) addrinfo{};
    if (!dst) {
        return makeAddressInfo(nullptr);
    }

    dst->ai_flags = src->ai_flags;
    dst->ai_family = src->ai_family;
    dst->ai_socktype = src->ai_socktype;
    dst->ai_protocol = src->ai_protocol;
    dst->ai_addrlen = src->ai_addrlen;
    dst->ai_next = nullptr;
    dst->ai_canonname = nullptr;

    dst->ai_addr = reinterpret_cast<sockaddr*>(new (std::nothrow) Byte[src->ai_addrlen]{});
    if (!dst->ai_addr) {
        delete dst;
        return makeAddressInfo(nullptr);
    }
    std::memcpy(dst->ai_addr, src->ai_addr, src->ai_addrlen);

    return AddressInfoPtr(dst, [](addrinfo* p) {
        if (p) {
            delete[] reinterpret_cast<Byte*>(p->ai_addr);
            delete p;
        }
    });
}

}  // namespace slark::http
