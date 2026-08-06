//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#include "M3U8Parser.h"

#include <regex>
#include <sstream>
#include <utility>

#include "Log.hpp"
#include "StringUtil.h"

namespace slark {

static const std::regex kVariantRegex(
    R"(#EXT-X-STREAM-INF:.*BANDWIDTH=(\d+).*?(?:,RESOLUTION=(\d+)x(\d+))?\s*\n([^\n]+))");
static const std::regex kExtinfRegex(R"(#EXTINF:([\d.]+)\s*(?:,([^\n]+))?\s*\n([^\n]+))");
static const std::regex kEndListRegex(R"(#EXT-X-ENDLIST)");
static const std::regex kDiscontinuityRegex(R"(#EXT-X-DISCONTINUITY)");
static const std::regex kByteRangeRegex(R"(#EXT-X-BYTERANGE:(\d+)@(\d+))");

M3U8Parser::M3U8Parser(std::string baseUrl) : baseUrl_(std::move(baseUrl)) {
    playlist_ = std::make_shared<MediaPlaylist>();
}

void M3U8Parser::reset() noexcept {
    isMasterPlaylist_ = false;
    isCompleted_ = false;
    playlist_ = std::make_shared<MediaPlaylist>();
}

bool M3U8Parser::parse(const std::string& content) noexcept {
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

bool parseExtinfLine(const std::string& line,
                     std::istringstream& stream,
                     double& currentTime,
                     bool& hasDiscontinuity,
                     std::optional<Range>& pendingByteRange,
                     const std::string& baseUrl,
                     std::shared_ptr<MediaPlaylist>& playlist) noexcept {
    std::smatch match;
    if (!std::regex_match(line, match,
                          std::regex(R"(#EXTINF:([\d.]+)\s*(?:,([^\n]*))?)"))) {
        return false;
    }
    auto durResult = StringUtil::fromChars<double>(match[1].str());
    if (!durResult || *durResult < 0.0 || *durResult > 86400.0) {
        return true;
    }

    Segment segment;
    segment.duration = *durResult;
    segment.startTime = currentTime;
    segment.isDiscontinuity = hasDiscontinuity;

    hasDiscontinuity = false;

    if (pendingByteRange.has_value()) {
        segment.byteRange = pendingByteRange;
        pendingByteRange.reset();
    }

    std::string uriLine;
    if (std::getline(stream, uriLine)) {
        uriLine.erase(0, uriLine.find_first_not_of(" \t\r"));
        uriLine.erase(uriLine.find_last_not_of(" \t\r") + 1);
        segment.uri = M3U8Parser::resolveUrl(baseUrl, uriLine);
    }

    playlist->addSegment(std::move(segment));
    currentTime += segment.duration;
    return true;
}

}  // namespace

bool M3U8Parser::parseMediaPlaylist(const std::string& content) noexcept {
    if (!std::regex_search(content, kEndListRegex)) {
        playlist_->setLive(true);
    }

    double currentTime = 0.0;
    bool hasDiscontinuity = false;
    std::optional<Range> pendingByteRange;
    uint64_t accumulatedOffset = 0;
    bool hasAccumulatedOffset = false;

    std::istringstream stream(content);
    std::string line;
    while (std::getline(stream, line)) {
        line.erase(0, line.find_first_not_of(" \t\r"));
        line.erase(line.find_last_not_of(" \t\r") + 1);

        if (line.empty()) {
            continue;
        }

        if (line.find("#EXTINF:") == 0) {
            parseExtinfLine(line, stream, currentTime, hasDiscontinuity,
                            pendingByteRange, baseUrl_, playlist_);
        } else if (line.find("#EXT-X-BYTERANGE:") == 0) {
            parseByteRangeLine(line, pendingByteRange, accumulatedOffset, hasAccumulatedOffset);
        } else if (line.find("#EXT-X-DISCONTINUITY") == 0) {
            hasDiscontinuity = true;
        }
    }

    playlist_->setTotalDuration(currentTime);
    isCompleted_ = std::regex_search(content, kEndListRegex);

    return !playlist_->segments().empty();
}

std::string M3U8Parser::resolveUrl(const std::string& baseUrl,
                                   const std::string& relativeUrl) noexcept {
    if (relativeUrl.find("://") != std::string::npos) {
        return relativeUrl;
    }

    auto url = relativeUrl;
    std::string base = baseUrl;

    if (url.starts_with("../")) {
        while (url.starts_with("../")) {
            url.erase(0, 3);
            auto pos = base.rfind('/');
            if (pos != std::string::npos) {
                base = base.substr(0, pos);
            } else {
                break;
            }
        }
        return base + "/" + url;
    }

    if (url.starts_with('/')) {
        auto protocolEnd = base.find("://");
        if (protocolEnd != std::string::npos) {
            protocolEnd += 3;
            auto hostEnd = base.find('/', protocolEnd);
            if (hostEnd != std::string::npos) {
                return base.substr(0, hostEnd) + url;
            }
        }
        return url;
    }

    while (url.starts_with("./")) {
        url.erase(0, 2);
    }

    if (base.back() != '/') {
        auto pos = base.rfind('/');
        if (pos != std::string::npos) {
            base = base.substr(0, pos + 1);
        }
    }

    return base + url;
}

}  // namespace slark
