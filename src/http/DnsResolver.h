// Copyright (c) 2026 Nevermore-. All rights reserved.

#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <list>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

#include "Awaitable.hpp"
#include "CancellationToken.h"
#include "Socket.hpp"
#include "Task.hpp"
#include "ThreadPool.hpp"

namespace slark::http {

struct DnsCacheEntry {
    AddressInfoPtr address;
    std::chrono::steady_clock::time_point expireAt;
};

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

    Task<AddressInfoPtr> resolveAsync(const std::string& host,
                                      const std::string& port,
                                      IPVersion ipVersion,
                                      CancellationToken token = {});

    void invalidate(const std::string& host) noexcept;
    void clearCache() noexcept;

 private:
    struct State;

    static Task<AddressInfoPtr> resolveAsyncImpl(std::shared_ptr<State> state,
                                                 std::string host,
                                                 std::string port,
                                                 IPVersion ipVersion,
                                                 CancellationToken token);

    static std::string makeKey(const std::string& host,
                               const std::string& port,
                               IPVersion ipVersion);
    static AddressInfoPtr lookupCache(State& state, const std::string& key) noexcept;
    static void insertCache(State& state,
                            const std::string& key,
                            const AddressInfoPtr& addr);
    static AddressInfoPtr cloneAddrInfo(const addrinfo* src) noexcept;

    std::shared_ptr<State> state_;
};

}  // namespace slark::http
