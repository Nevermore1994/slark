//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#include "M3U8Parser.h"

#include <algorithm>
#include <cctype>
#include <regex>
#include <sstream>
#include <string_view>
#include <utility>
#include <vector>

#include "Log.hpp"
#include "StringUtil.h"

namespace slark {

static const std::regex kVariantRegex(
    R"(#EXT-X-STREAM-INF:.*BANDWIDTH=(\d+).*?(?:,RESOLUTION=(\d+)x(\d+))?\s*\n([^\n]+))");
static const std::regex kEndListRegex(R"(#EXT-X-ENDLIST)");
static const std::regex kMediaSequenceRegex(R"(#EXT-X-MEDIA-SEQUENCE:(\d+))");
static const std::regex kTargetDurationRegex(R"(#EXT-X-TARGETDURATION:([\d.]+))");

M3U8Parser::M3U8Parser(std::string baseUrl) : baseUrl_(std::move(baseUrl)) {
    playlist_ = std::make_shared<MediaPlaylist>();
}

void M3U8Parser::reset() noexcept {
    isMasterPlaylist_ = false;
    isCompleted_ = false;
    playlist_ = std::make_shared<MediaPlaylist>();
}

bool M3U8Parser::parse(const std::string& content) noexcept {
    reset();
    constexpr size_t kMaxM3U8Size = 1 * 1024 * 1024;  // 1MB
    if (content.size() > kMaxM3U8Size) {
        LogE("M3U8Parser: content too large: {} bytes", content.size());
        return false;
    }
    if (content.find("#EXT-X-STREAM-INF") != std::string::npos) {
        return parseMasterPlaylist(content);
    } else {
        return parseMediaPlaylist(content);
    }
}

bool M3U8Parser::parseMasterPlaylist(const std::string& content) noexcept {
    isMasterPlaylist_ = true;
    std::smatch match;
    std::string::const_iterator searchStart(content.cbegin());

    while (std::regex_search(searchStart, content.cend(), match, kVariantRegex)) {
        Variant variant;
        auto bwStr = match[1].str();
        auto bwResult = StringUtil::fromChars<uint32_t>(bwStr);
        if (!bwResult) {
            searchStart = match.suffix().first;
            continue;
        }
        variant.bandwidth = *bwResult;
        variant.uri = resolveUrl(baseUrl_, match[4].str());

        if (match[2].matched && match[3].matched) {
            auto wStr = match[2].str();
            auto hStr = match[3].str();
            auto wResult = StringUtil::fromChars<uint32_t>(wStr);
            auto hResult = StringUtil::fromChars<uint32_t>(hStr);
            if (wResult && hResult) {
                variant.width = *wResult;
                variant.height = *hResult;
            }
        }

        playlist_->addVariant(std::move(variant));
        searchStart = match.suffix().first;
    }

    isCompleted_ = true;
    return !playlist_->variants().empty();
}

namespace {

bool parseByteRangeLine(const std::string& line,
                        std::optional<Range>& pendingByteRange,
                        uint64_t& accumulatedOffset,
                        bool& hasAccumulatedOffset) noexcept {
    std::smatch match;
    if (!std::regex_match(line, match, std::regex(R"(#EXT-X-BYTERANGE:(\d+)(?:@(\d+))?)"))) {
        return false;
    }
    Range range;
    auto lenResult = StringUtil::fromChars<uint64_t>(match[1].str());
    if (!lenResult) {
        return true;
    }
    range.size = static_cast<int64_t>(*lenResult);

    if (match[2].matched) {
        auto offResult = StringUtil::fromChars<uint64_t>(match[2].str());
        if (!offResult) {
            return true;
        }
        range.pos = *offResult;
        accumulatedOffset = *offResult + *lenResult;
        hasAccumulatedOffset = true;
    } else if (hasAccumulatedOffset) {
        range.pos = accumulatedOffset;
        accumulatedOffset += *lenResult;
    } else {
        return true;
    }
    pendingByteRange = range;
    return true;
}

std::optional<double> parseExtinfDuration(const std::string& line) noexcept {
    std::smatch match;
    if (!std::regex_match(line, match,
                          std::regex(R"(#EXTINF:([\d.]+)\s*(?:,([^\n]*))?)"))) {
        return std::nullopt;
    }
    auto duration = StringUtil::fromChars<double>(match[1].str());
    if (!duration || *duration < 0.0 || *duration > 86400.0) {
        return std::nullopt;
    }
    return *duration;
}

}  // namespace

bool M3U8Parser::parseMediaPlaylist(const std::string& content) noexcept {
    std::smatch match;
    if (std::regex_search(content, match, kMediaSequenceRegex)) {
        if (const auto sequence = StringUtil::fromChars<uint64_t>(match[1].str())) {
            playlist_->setMediaSequence(*sequence);
        }
    }
    if (std::regex_search(content, match, kTargetDurationRegex)) {
        if (const auto duration = StringUtil::fromChars<double>(match[1].str());
            duration && *duration >= 0.0 && *duration <= 86400.0) {
            playlist_->setTargetDuration(*duration);
        }
    }

    const bool isLive = !std::regex_search(content, kEndListRegex);
    playlist_->setLive(isLive);

    double currentTime = 0.0;
    std::optional<double> pendingDuration;
    bool pendingDiscontinuity = false;
    bool hasDiscontinuity = false;
    std::optional<Range> pendingByteRange;
    uint64_t accumulatedOffset = 0;
    bool hasAccumulatedOffset = false;

    std::istringstream stream(content);
    std::string line;
    while (std::getline(stream, line)) {
        const auto first = line.find_first_not_of(" \t\r");
        if (first == std::string::npos) {
            continue;
        }
        const auto last = line.find_last_not_of(" \t\r");
        line = line.substr(first, last - first + 1);

        if (line.starts_with("#EXTINF:")) {
            if (const auto duration = parseExtinfDuration(line)) {
                pendingDuration = *duration;
                pendingDiscontinuity = hasDiscontinuity;
                hasDiscontinuity = false;
            }
        } else if (line.starts_with("#EXT-X-BYTERANGE:")) {
            parseByteRangeLine(line, pendingByteRange, accumulatedOffset, hasAccumulatedOffset);
        } else if (line.starts_with("#EXT-X-DISCONTINUITY")) {
            hasDiscontinuity = true;
        } else if (!line.starts_with('#') && pendingDuration) {
            Segment segment;
            segment.duration = *pendingDuration;
            segment.startTime = currentTime;
            segment.isDiscontinuity = pendingDiscontinuity;
            segment.byteRange = pendingByteRange;
            segment.uri = M3U8Parser::resolveUrl(baseUrl_, line);
            playlist_->addSegment(std::move(segment));
            currentTime += *pendingDuration;
            pendingDuration.reset();
            pendingDiscontinuity = false;
            pendingByteRange.reset();
        }
    }

    playlist_->setTotalDuration(currentTime);
    isCompleted_ = !isLive;

    return !playlist_->segments().empty();
}

std::string M3U8Parser::resolveUrl(const std::string& baseUrl,
                                   const std::string& relativeUrl) noexcept {
    if (relativeUrl.empty()) {
        return baseUrl;
    }

    const auto schemePos = relativeUrl.find(':');
    const auto delimiterPos = relativeUrl.find_first_of("/?#");
    if (schemePos != std::string::npos &&
        (delimiterPos == std::string::npos || schemePos < delimiterPos)) {
        return relativeUrl;
    }

    const auto fragmentPos = relativeUrl.find('#');
    const auto referenceWithoutFragment =
        relativeUrl.substr(0, fragmentPos == std::string::npos ? relativeUrl.size() : fragmentPos);
    const auto fragment = fragmentPos == std::string::npos
                              ? std::string{}
                              : relativeUrl.substr(fragmentPos);
    const auto queryPos = referenceWithoutFragment.find('?');
    const auto referencePath = referenceWithoutFragment.substr(
        0, queryPos == std::string::npos ? referenceWithoutFragment.size() : queryPos);
    const auto query = queryPos == std::string::npos
                           ? std::string{}
                           : referenceWithoutFragment.substr(queryPos);

    const auto baseFragmentPos = baseUrl.find('#');
    const auto baseWithoutFragment =
        baseUrl.substr(0, baseFragmentPos == std::string::npos ? baseUrl.size() : baseFragmentPos);
    const auto baseQueryPos = baseWithoutFragment.find('?');
    const auto baseWithoutQuery = baseWithoutFragment.substr(
        0, baseQueryPos == std::string::npos ? baseWithoutFragment.size() : baseQueryPos);
    if (relativeUrl.starts_with('#')) {
        return baseWithoutFragment + fragment;
    }
    const auto baseSchemePos = baseWithoutQuery.find("://");

    if (referencePath.starts_with("//") && baseSchemePos != std::string::npos) {
        return baseWithoutQuery.substr(0, baseSchemePos) + ":" + referenceWithoutFragment +
               fragment;
    }

    if (baseSchemePos == std::string::npos) {
        return relativeUrl.starts_with('/') ? relativeUrl : baseUrl + relativeUrl;
    }

    const auto authorityStart = baseSchemePos + 3;
    const auto pathStart = baseWithoutQuery.find('/', authorityStart);
    const auto origin = pathStart == std::string::npos
                            ? baseWithoutQuery
                            : baseWithoutQuery.substr(0, pathStart);
    const auto basePath = pathStart == std::string::npos
                              ? std::string("/")
                              : baseWithoutQuery.substr(pathStart);

    const auto normalizePath = [](const std::string& path) {
        const bool absolute = path.starts_with('/');
        const bool trailingSlash = path.ends_with('/');
        std::vector<std::string> components;
        size_t begin = 0;
        while (begin <= path.size()) {
            const auto end = path.find('/', begin);
            const auto component = path.substr(begin, end == std::string::npos ? end : end - begin);
            if (component.empty() || component == ".") {
            } else if (component == "..") {
                if (!components.empty() && components.back() != "..") {
                    components.pop_back();
                } else if (!absolute) {
                    components.emplace_back(component);
                }
            } else {
                components.emplace_back(component);
            }
            if (end == std::string::npos) {
                break;
            }
            begin = end + 1;
        }

        std::string normalized = absolute ? "/" : "";
        for (size_t i = 0; i < components.size(); ++i) {
            if (i > 0) {
                normalized += '/';
            }
            normalized += components[i];
        }
        if (trailingSlash && !normalized.ends_with('/')) {
            normalized += '/';
        }
        if (normalized.empty() && absolute) {
            normalized = "/";
        }
        return normalized;
    };

    if (referencePath.empty()) {
        return origin + normalizePath(basePath) + query + fragment;
    }

    const auto resolvedPath = referencePath.starts_with('/')
                                  ? normalizePath(referencePath)
                                  : normalizePath(basePath.substr(0, basePath.rfind('/') + 1) +
                                                   referencePath);
    return origin + resolvedPath + query + fragment;
}

}  // namespace slark
