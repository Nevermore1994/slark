// Copyright (c) 2026 Nevermore-. All rights reserved.

#include "DnsResolver.h"

#include <netdb.h>

#include <cstring>
#include <new>

#include "Log.hpp"

namespace slark::http {

struct DnsResolver::State {
    explicit State(const Config& configIn)
        : config(configIn), threadPool(ThreadPoolConfig{config.threadCount}) {}

    Config config;
    ThreadPool threadPool;
    std::mutex cacheMutex;
    std::list<std::string> lruList;

    struct CacheValue {
        DnsCacheEntry entry;
        std::list<std::string>::iterator lruIt;
    };

    std::unordered_map<std::string, CacheValue> cache;
};

DnsResolver& DnsResolver::instance() noexcept {
    static DnsResolver instance;
    return instance;
}

DnsResolver::DnsResolver(const Config& config) : state_(std::make_shared<State>(config)) {}

Task<AddressInfoPtr> DnsResolver::resolveAsync(const std::string& host,
                                               const std::string& port,
                                               IPVersion ipVersion,
                                               CancellationToken token) {
    return resolveAsyncImpl(state_, host, port, ipVersion, std::move(token));
}

Task<AddressInfoPtr> DnsResolver::resolveAsyncImpl(std::shared_ptr<State> state,
                                                    std::string host,
                                                    std::string port,
                                                    IPVersion ipVersion,
                                                    CancellationToken token) {
    auto key = makeKey(host, port, ipVersion);

    {
        std::lock_guard lock(state->cacheMutex);
        if (auto cached = lookupCache(*state, key)) {
            LogI("[DnsResolver::resolveAsync] cache hit: {}:{} ipv={}", host, port,
                 static_cast<int>(ipVersion));
            co_return cached;
        }
    }

    if (token.isCancelled()) {
        co_return makeAddressInfo(nullptr);
    }

    LogI("[DnsResolver::resolveAsync] resolving: {}:{} ipv={}", host, port,
         static_cast<int>(ipVersion));

    auto awaiter = std::make_shared<CompletionAwaiter<AddressInfoPtr>>();
    // Cancellation propagation (C-06): a token cancel while DNS is suspended ends
    // early without waiting for the thread pool
    auto cancelReg = token.registerCallback([awaiter]() {
        awaiter->complete(makeAddressInfo(nullptr));
    });
    auto future = state->threadPool.submit([host, port, ipVersion, awaiter]() {
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

    AddressInfoPtr resolved = co_await *awaiter;

    if (resolved) {
        std::lock_guard lock(state->cacheMutex);
        insertCache(*state, key, resolved);
        LogI("[DnsResolver::resolveAsync] resolved & cached: {}:{}", host, port);
    } else {
        LogE("[DnsResolver::resolveAsync] resolve returned null: {}:{}", host, port);
    }

    co_return resolved;
}

void DnsResolver::invalidate(const std::string& host) noexcept {
    std::lock_guard lock(state_->cacheMutex);
    auto it = state_->cache.begin();
    while (it != state_->cache.end()) {
        if (it->first.starts_with(host + ":")) {
            state_->lruList.erase(it->second.lruIt);
            it = state_->cache.erase(it);
        } else {
            ++it;
        }
    }
}

void DnsResolver::clearCache() noexcept {
    std::lock_guard lock(state_->cacheMutex);
    state_->cache.clear();
    state_->lruList.clear();
}

std::string DnsResolver::makeKey(const std::string& host,
                                 const std::string& port,
                                 IPVersion ipVersion) {
    return host + ":" + port + ":" + std::to_string(static_cast<uint8_t>(ipVersion));
}

AddressInfoPtr DnsResolver::lookupCache(State& state, const std::string& key) noexcept {
    auto it = state.cache.find(key);
    if (it == state.cache.end()) {
        return makeAddressInfo(nullptr);
    }

    auto& val = it->second;
    if (std::chrono::steady_clock::now() > val.entry.expireAt) {
        state.lruList.erase(val.lruIt);
        state.cache.erase(it);
        return makeAddressInfo(nullptr);
    }

    state.lruList.splice(state.lruList.begin(), state.lruList, val.lruIt);
    return cloneAddrInfo(val.entry.address.get());
}

void DnsResolver::insertCache(State& state,
                              const std::string& key,
                              const AddressInfoPtr& addr) {
    if (state.config.maxCacheSize == 0) {
        return;
    }

    if (auto it = state.cache.find(key); it != state.cache.end()) {
        state.lruList.erase(it->second.lruIt);
        state.cache.erase(it);
    }

    if (state.cache.size() >= state.config.maxCacheSize) {
        auto& evictKey = state.lruList.back();
        state.cache.erase(evictKey);
        state.lruList.pop_back();
    }

    state.lruList.push_front(key);
    state.cache.insert_or_assign(
        key,
        State::CacheValue{
            .entry = {.address = cloneAddrInfo(addr.get()),
                      .expireAt = std::chrono::steady_clock::now() + state.config.cacheTtl},
            .lruIt = state.lruList.begin(),
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
