//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#include "StreamFactory.hpp"

#include <algorithm>
#include <string_view>

#include "FileStream.h"
#include "HLSStream.h"
#include "HttpStream.h"

namespace slark {

FactoryResult<std::shared_ptr<IStream>> StreamFactory::createStream(
    const std::string& uri, std::shared_ptr<Thread> ownerThread) {
    auto type = detectStreamType(uri);

    switch (type) {
        case StreamType::File:
            return createFileStream(uri);
        case StreamType::Http:
            return createHttpStream(uri, std::move(ownerThread));
        case StreamType::HLS:
            return createHLSStream(uri, std::move(ownerThread));
        default:
            return std::unexpected(
                FactoryError{.code = -1, .message = "Unknown stream type for URI: " + uri});
    }
}

FactoryResult<std::shared_ptr<IStream>> StreamFactory::createFileStream(
    [[maybe_unused]] const std::string& uri) {
    auto stream = std::make_shared<FileStream>();
    return stream;
}

FactoryResult<std::shared_ptr<IStream>> StreamFactory::createHttpStream(
    [[maybe_unused]] const std::string& uri, std::shared_ptr<Thread> ownerThread) {
    auto stream = std::make_shared<HttpStream>(std::move(ownerThread));
    return stream;
}

FactoryResult<std::shared_ptr<IStream>> StreamFactory::createHLSStream(
    [[maybe_unused]] const std::string& uri, std::shared_ptr<Thread> ownerThread) {
    auto stream = std::make_shared<HLSStream>(std::move(ownerThread));
    return stream;
}

StreamType StreamFactory::detectStreamType(const std::string& uri) {
    if (isHLS(uri)) {
        return StreamType::HLS;
    }

    if (isLocalFile(uri)) {
        return StreamType::File;
    }

    if (isHttp(uri)) {
        return StreamType::Http;
    }

    return StreamType::File;
}

bool StreamFactory::isHLS(const std::string& uri) noexcept {
    std::string_view sv(uri);
    auto matchSuffix = [sv](std::string_view suffix) {
        if (sv.size() < suffix.size()) {
            return false;
        }
        auto sub = sv.substr(sv.size() - suffix.size());
        return std::equal(sub.begin(), sub.end(), suffix.begin(), suffix.end(), [](char a, char b) {
            return std::tolower(static_cast<unsigned char>(a)) ==
                   std::tolower(static_cast<unsigned char>(b));
        });
    };
    return matchSuffix(".m3u8") || matchSuffix(".m3u");
}

bool StreamFactory::isLocalFile(const std::string& uri) noexcept {
    std::string_view sv(uri);

    if (sv.starts_with("file:")) {
        return true;
    }

    if (sv.find(":") == std::string_view::npos) {
        return true;
    }

    return false;
}

bool StreamFactory::isHttp(const std::string& uri) noexcept {
    std::string_view sv(uri);

    auto toLower = [](char c) {
        return static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    };

    std::string prefix;
    for (size_t i = 0; i < std::min(sv.size(), size_t(8)); ++i) {
        prefix += toLower(sv[i]);
    }

    return prefix.starts_with("http:") || prefix.starts_with("https:");
}

}  // namespace slark
