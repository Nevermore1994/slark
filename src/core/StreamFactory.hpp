//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//
#pragma once

#include "../base/IStream.hpp"

#include <expected>
#include <memory>
#include <string>

namespace slark {

struct FactoryError {
    int32_t code = 0;
    std::string message;
};

template <typename T>
using FactoryResult = std::expected<T, FactoryError>;

class StreamFactory {
public:
    static FactoryResult<std::shared_ptr<IStream>> createStream(const std::string& uri);

    static FactoryResult<std::shared_ptr<IStream>> createStream(const std::string& uri,
                                                                const OpenParams& params);

    static FactoryResult<std::shared_ptr<IStream>> createFileStream(const std::string& uri);

    static FactoryResult<std::shared_ptr<IStream>> createHttpStream(const std::string& uri);

    static FactoryResult<std::shared_ptr<IStream>> createHLSStream(const std::string& uri);

    [[nodiscard]] static StreamType detectStreamType(const std::string& uri);

    [[nodiscard]] static bool isHLS(const std::string& uri) noexcept;

    [[nodiscard]] static bool isLocalFile(const std::string& uri) noexcept;

    [[nodiscard]] static bool isHttp(const std::string& uri) noexcept;
};

} // namespace slark
namespace slark
