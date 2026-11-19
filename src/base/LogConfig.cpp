//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//
#include "LogConfig.h"

#include <atomic>
#include <memory>
#include <mutex>
#include <vector>

#include "ConfigDefaults.h"
#include "Log.hpp"

namespace slark {

namespace internal {
constinit std::atomic<int> gMinLogLevel{kMinLogLevelDefault};
}  // namespace internal

namespace {

// Snapshots outlive their readers: callers hold bare references across file I/O.
constinit std::atomic<const LogConfig*> gCurrentConfig{nullptr};

struct ConfigStore {
    std::mutex mutex;
    std::vector<std::unique_ptr<const LogConfig>> snapshots;
};

ConfigStore& configStore() {
    static ConfigStore store;
    return store;
}

}  // namespace

void setLogConfig(const LogConfig& config) {
    auto snapshot = std::make_unique<const LogConfig>(config);
    const auto* published = snapshot.get();
    auto& store = configStore();
    {
        std::lock_guard<std::mutex> lock(store.mutex);
        store.snapshots.push_back(std::move(snapshot));
        internal::gMinLogLevel.store(config.minLogLevel, std::memory_order_relaxed);
        gCurrentConfig.store(published, std::memory_order_release);
    }
}

const LogConfig& getLogConfig() {
    if (const auto* config = gCurrentConfig.load(std::memory_order_acquire)) {
        return *config;
    }
    static const LogConfig defaultConfig;
    return defaultConfig;
}

}  // namespace slark
