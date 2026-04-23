//
// Created by Nevermore on 2024/5/12.
// slark IOBase
// Copyright (c) 2024 Nevermore All rights reserved.
//

#pragma once
#include "Data.h"
#include "IOBase.h"
#include "MoveOnlyFunction.hpp"
#include "NonCopyable.h"
#include "Range.h"

#include <mutex>
#include <string>

namespace slark {

struct IReader;
using ReaderDataCallBack = MoveOnlyFunction<void(IReader*, DataPacket, IOState)>;

enum class ReaderType : uint8_t {
    Local,
    NetWork,
    HLS,
};

constexpr uint64_t kReadDefaultSize = 1024 * 64; // 64kb

struct ReaderTask {
    std::string path;
    ReaderDataCallBack callBack;
    Range range;
    uint64_t readBlockSize = kReadDefaultSize; // only local
    std::chrono::milliseconds timeInterval{5}; // only local

    explicit ReaderTask(ReaderDataCallBack&& func) : callBack(std::move(func)) {}
};

using ReaderTaskPtr = std::unique_ptr<ReaderTask>;

struct IReader : public NonCopyMovable {
    ~IReader() override = default;

    virtual bool open(ReaderTaskPtr) noexcept = 0;

    virtual void updateReadRange(Range range) noexcept = 0;

    [[nodiscard]] virtual IOState state() noexcept = 0;

    virtual void reset() noexcept = 0;

    virtual void close() noexcept = 0;

    virtual void start() noexcept = 0;

    virtual void pause() noexcept = 0;

    [[nodiscard]] virtual bool isCompleted() noexcept = 0;

    [[nodiscard]] virtual bool isRunning() noexcept = 0;

    virtual void seek(uint64_t pos) noexcept = 0;

    [[nodiscard]] virtual uint64_t size() noexcept = 0;

    [[nodiscard]] ReaderType type() const noexcept {
        return type_;
    }

    [[nodiscard]] bool isLocal() const noexcept {
        return type_ == ReaderType::Local;
    }

    [[nodiscard]] bool isNetwork() const noexcept {
        return type_ != ReaderType::Local;
    }

protected:
    ReaderType type_;
    std::mutex taskMutex_;
    ReaderTaskPtr task_;
};

} // namespace slark
