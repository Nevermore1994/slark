//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//
#pragma once

#include <expected>
#include <memory>
#include <string>

#include "IStream.h"

namespace slark {

class Thread;

struct FactoryError {
    int32_t code = 0;
    std::string message;
};

template <typename T>
using FactoryResult = std::expected<T, FactoryError>;

class StreamFactory {
 public:
    // ownerThread：网络流（HttpStream/HLSStream）构造函数注入，本地文件流忽略
    static FactoryResult<std::shared_ptr<IStream>> createStream(
        const std::string& uri, std::shared_ptr<Thread> ownerThread = nullptr);

    static FactoryResult<std::shared_ptr<IStream>> createFileStream(const std::string& uri);

    static FactoryResult<std::shared_ptr<IStream>> createHttpStream(
        const std::string& uri, std::shared_ptr<Thread> ownerThread = nullptr);

    static FactoryResult<std::shared_ptr<IStream>> createHLSStream(
        const std::string& uri, std::shared_ptr<Thread> ownerThread = nullptr);

    [[nodiscard]] static StreamType detectStreamType(const std::string& uri);

    [[nodiscard]] static bool isHLS(const std::string& uri) noexcept;

    [[nodiscard]] static bool isLocalFile(const std::string& uri) noexcept;

    [[nodiscard]] static bool isHttp(const std::string& uri) noexcept;
};

}  // namespace slark
