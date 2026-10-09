//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//
#pragma once

#include <cstdint>
#include <cstring>
#include <future>
#include <memory>
#include <thread>
#include <vector>

#include "Buffer.hpp"
#include "Data.h"

namespace slark::test {

using Bytes = std::vector<uint8_t>;

inline void be16(Bytes& out, uint16_t v) {
    out.push_back(static_cast<uint8_t>(v >> 8));
    out.push_back(static_cast<uint8_t>(v));
}

inline void be32(Bytes& out, uint32_t v) {
    out.push_back(static_cast<uint8_t>(v >> 24));
    out.push_back(static_cast<uint8_t>(v >> 16));
    out.push_back(static_cast<uint8_t>(v >> 8));
    out.push_back(static_cast<uint8_t>(v));
}

inline void be64(Bytes& out, uint64_t v) {
    be32(out, static_cast<uint32_t>(v >> 32));
    be32(out, static_cast<uint32_t>(v));
}

inline void fourcc(Bytes& out, const char* type) {
    out.insert(out.end(), type, type + 4);
}

inline Bytes box(const char* type, const Bytes& payload) {
    Bytes out;
    be32(out, static_cast<uint32_t>(payload.size() + 8));
    fourcc(out, type);
    out.insert(out.end(), payload.begin(), payload.end());
    return out;
}

inline Bytes concat(std::initializer_list<Bytes> parts) {
    Bytes out;
    for (const auto& part : parts) {
        out.insert(out.end(), part.begin(), part.end());
    }
    return out;
}

inline DataPtr makeData(const Bytes& bytes) {
    auto data = Data::create(bytes.size());
    if (!bytes.empty()) {
        std::memcpy(data->data(), bytes.data(), bytes.size());
    }
    return data;
}

inline std::unique_ptr<Buffer> makeBuffer(const Bytes& bytes, uint64_t totalSize = 0) {
    auto buffer = totalSize == 0 ? std::make_unique<Buffer>() : std::make_unique<Buffer>(totalSize);
    buffer->append(0, makeData(bytes));
    return buffer;
}

// Runs fn on a detached thread that owns its state, so a regression that loops forever fails
// the test on timeout instead of hanging the process in a join or future destructor.
template <typename Result, typename Fn>
std::future<Result> runDetached(Fn fn) {
    auto promise = std::make_shared<std::promise<Result>>();
    auto future = promise->get_future();
    std::thread([promise, fn = std::move(fn)]() mutable {
        promise->set_value(fn());
    }).detach();
    return future;
}

}  // namespace slark::test
