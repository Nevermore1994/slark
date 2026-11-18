//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#include "M3U8Parser.h"

#include <algorithm>
#include <cctype>
#include <initializer_list>
#include <regex>
#include <string_view>
#include <utility>
#include <vector>

#include "Log.hpp"
#include "StringUtil.h"

namespace slark {

namespace {

[[nodiscard]] std::pair<const char*, const char*> charRange(std::string_view text) noexcept {
    const char* begin = text.empty() ? "" : text.data();
    return {begin, begin + text.size()};
}

[[nodiscard]] std::string concatParts(std::initializer_list<std::string_view> parts) noexcept {
    size_t total = 0;
    for (const auto part : parts) {
        total += part.size();
    }
    std::string result;
    result.reserve(total);
    for (const auto part : parts) {
        result.append(part);
    }
    return result;
}

}  // namespace

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

bool M3U8Parser::parse(std::string_view content) noexcept {
    reset();
    constexpr size_t kMaxM3U8Size = 1 * 1024 * 1024;  // 1MB
    if (content.size() > kMaxM3U8Size) {
        LogE("M3U8Parser: content too large: {} bytes", content.size());
        return false;
    }
    if (content.find("#EXT-X-STREAM-INF") != std::string_view::npos) {
        return parseMasterPlaylist(content);
    } else {
        return parseMediaPlaylist(content);
    }
}

bool M3U8Parser::parseMasterPlaylist(std::string_view content) noexcept {
    isMasterPlaylist_ = true;
    std::cmatch match;
    const auto [begin, end] = charRange(content);
    const char* searchStart = begin;

    while (std::regex_search(searchStart, end, match, kVariantRegex)) {
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

constexpr std::string_view kByteRangeTag = "#EXT-X-BYTERANGE:";
constexpr std::string_view kExtinfTag = "#EXTINF:";

[[nodiscard]] constexpr bool isAsciiDigit(char c) noexcept {
    return c >= '0' && c <= '9';
}

[[nodiscard]] bool isUnsignedInteger(std::string_view text) noexcept {
    return !text.empty() && std::ranges::all_of(text, isAsciiDigit);
}

bool parseByteRangeLine(std::string_view line,
                        std::optional<Range>& pendingByteRange,
                        uint64_t& accumulatedOffset,
                        bool& hasAccumulatedOffset) noexcept {
    if (!line.starts_with(kByteRangeTag)) {
        return false;
    }
    const auto payload = line.substr(kByteRangeTag.size());
    const auto separator = payload.find('@');
    const auto lengthText = payload.substr(0, separator);
    const auto offsetText = separator == std::string_view::npos
                                ? std::string_view{}
                                : payload.substr(separator + 1);
    if (!isUnsignedInteger(lengthText) ||
        (separator != std::string_view::npos && !isUnsignedInteger(offsetText))) {
        return false;
    }

    const auto length = StringUtil::fromChars<uint64_t>(lengthText);
    if (!length) {
        return true;
    }
    Range range;
    range.size = static_cast<int64_t>(*length);

    if (separator != std::string_view::npos) {
        const auto offset = StringUtil::fromChars<uint64_t>(offsetText);
        if (!offset) {
            return true;
        }
        range.pos = *offset;
        accumulatedOffset = *offset + *length;
        hasAccumulatedOffset = true;
    } else if (hasAccumulatedOffset) {
        range.pos = accumulatedOffset;
        accumulatedOffset += *length;
    } else {
        return true;
    }
    pendingByteRange = range;
    return true;
}

std::optional<double> parseExtinfDuration(std::string_view line) noexcept {
    if (!line.starts_with(kExtinfTag)) {
        return std::nullopt;
    }
    auto payload = line.substr(kExtinfTag.size());
    const auto digitEnd = std::ranges::find_if_not(payload, [](char c) {
        return isAsciiDigit(c) || c == '.';
    });
    const auto length = static_cast<size_t>(digitEnd - payload.begin());
    if (length == 0) {
        return std::nullopt;
    }
    const auto durationText = payload.substr(0, length);
    payload.remove_prefix(length);
    while (!payload.empty() && std::isspace(static_cast<unsigned char>(payload.front()))) {
        payload.remove_prefix(1);
    }
    if (!payload.empty() && payload.front() != ',') {
        return std::nullopt;
    }

    const auto duration = StringUtil::fromChars<double>(durationText);
    if (!duration || *duration < 0.0 || *duration > 86400.0) {
        return std::nullopt;
    }
    return *duration;
}

}  // namespace

bool M3U8Parser::parseMediaPlaylist(std::string_view content) noexcept {
    const auto [begin, end] = charRange(content);
    std::cmatch match;
    if (std::regex_search(begin, end, match, kMediaSequenceRegex)) {
        if (const auto sequence = StringUtil::fromChars<uint64_t>(match[1].str())) {
            playlist_->setMediaSequence(*sequence);
        }
    }
    if (std::regex_search(begin, end, match, kTargetDurationRegex)) {
        if (const auto duration = StringUtil::fromChars<double>(match[1].str());
            duration && *duration >= 0.0 && *duration <= 86400.0) {
            playlist_->setTargetDuration(*duration);
        }
    }

    const bool isLive = !std::regex_search(begin, end, kEndListRegex);
    playlist_->setLive(isLive);

    double currentTime = 0.0;
    std::optional<double> pendingDuration;
    bool pendingDiscontinuity = false;
    bool hasDiscontinuity = false;
    std::optional<Range> pendingByteRange;
    uint64_t accumulatedOffset = 0;
    bool hasAccumulatedOffset = false;

    size_t lineStart = 0;
    while (lineStart < content.size()) {
        const auto newline = content.find('\n', lineStart);
        const auto lineEnd = newline == std::string_view::npos ? content.size() : newline;
        auto line = content.substr(lineStart, lineEnd - lineStart);
        lineStart = lineEnd + 1;

        const auto first = line.find_first_not_of(" \t\r");
        if (first == std::string_view::npos) {
            continue;
        }
        const auto last = line.find_last_not_of(" \t\r");
        line = line.substr(first, last - first + 1);

        if (line.starts_with(kExtinfTag)) {
            if (const auto duration = parseExtinfDuration(line)) {
                pendingDuration = *duration;
                pendingDiscontinuity = hasDiscontinuity;
                hasDiscontinuity = false;
            }
        } else if (line.starts_with(kByteRangeTag)) {
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

std::string M3U8Parser::resolveUrl(std::string_view baseUrl,
                                   std::string_view relativeUrl) noexcept {
    if (relativeUrl.empty()) {
        return std::string(baseUrl);
    }

    const auto schemePos = relativeUrl.find(':');
    const auto delimiterPos = relativeUrl.find_first_of("/?#");
    if (schemePos != std::string_view::npos &&
        (delimiterPos == std::string_view::npos || schemePos < delimiterPos)) {
        return std::string(relativeUrl);
    }

    const auto fragmentPos = relativeUrl.find('#');
    const auto referenceWithoutFragment = relativeUrl.substr(0, fragmentPos);
    const auto fragment = fragmentPos == std::string_view::npos
                              ? std::string_view{}
                              : relativeUrl.substr(fragmentPos);
    const auto queryPos = referenceWithoutFragment.find('?');
    const auto referencePath = referenceWithoutFragment.substr(0, queryPos);
    const auto query = queryPos == std::string_view::npos
                           ? std::string_view{}
                           : referenceWithoutFragment.substr(queryPos);

    const auto baseFragmentPos = baseUrl.find('#');
    const auto baseWithoutFragment = baseUrl.substr(0, baseFragmentPos);
    const auto baseQueryPos = baseWithoutFragment.find('?');
    const auto baseWithoutQuery = baseWithoutFragment.substr(0, baseQueryPos);
    if (relativeUrl.starts_with('#')) {
        return concatParts({baseWithoutFragment, fragment});
    }
    const auto baseSchemePos = baseWithoutQuery.find("://");

    if (referencePath.starts_with("//") && baseSchemePos != std::string_view::npos) {
        return concatParts({baseWithoutQuery.substr(0, baseSchemePos), ":",
                            referenceWithoutFragment, fragment});
    }

    if (baseSchemePos == std::string_view::npos) {
        return relativeUrl.starts_with('/') ? std::string(relativeUrl)
                                            : concatParts({baseUrl, relativeUrl});
    }

    const auto authorityStart = baseSchemePos + 3;
    const auto pathStart = baseWithoutQuery.find('/', authorityStart);
    const auto origin = pathStart == std::string_view::npos
                            ? baseWithoutQuery
                            : baseWithoutQuery.substr(0, pathStart);
    constexpr std::string_view kRootPath{"/"};
    const auto basePath = pathStart == std::string_view::npos
                              ? kRootPath
                              : baseWithoutQuery.substr(pathStart);

    const auto normalizePath = [](std::string_view path) {
        const bool absolute = path.starts_with('/');
        const bool trailingSlash = path.ends_with('/');
        std::vector<std::string_view> components;
        size_t begin = 0;
        while (begin <= path.size()) {
            const auto end = path.find('/', begin);
            const auto component = path.substr(
                begin, end == std::string_view::npos ? end : end - begin);
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
            if (end == std::string_view::npos) {
                break;
            }
            begin = end + 1;
        }

        size_t length = absolute ? 1 : 0;
        for (const auto component : components) {
            length += component.size();
        }
        std::string normalized = absolute ? "/" : "";
        normalized.reserve(length + components.size() + (trailingSlash ? 1 : 0));
        for (size_t i = 0; i < components.size(); ++i) {
            if (i > 0) {
                normalized += '/';
            }
            normalized.append(components[i]);
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
        return concatParts({origin, normalizePath(basePath), query, fragment});
    }

    const auto resolvedPath =
        referencePath.starts_with('/')
            ? normalizePath(referencePath)
            : normalizePath(concatParts(
                  {basePath.substr(0, basePath.rfind('/') + 1), referencePath}));
    return concatParts({origin, resolvedPath, query, fragment});
}

}  // namespace slark
