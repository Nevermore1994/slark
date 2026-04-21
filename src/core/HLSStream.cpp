//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#include "HLSStream.h"

#include "../base/coroutine/Coroutine.hpp"
#include "../http/HttpUtil.hpp"

#include <algorithm>
#include <charconv>
#include <regex>
#include <unordered_set>

namespace slark {

// 文件级 static，只编译一次，所有函数共享
static const std::regex kVariantRegex(
    R"(#EXT-X-STREAM-INF:.*BANDWIDTH=(\d+).*?(?:,RESOLUTION=(\d+)x(\d+))?\s*
([^\n]+))");
static const std::regex kExtinfRegex(R"(#EXTINF:([\d.]+)\s*(?:,([^\n]+))?\s*
([^\n]+))");
static const std::regex kEndListRegex(R"(#EXT-X-ENDLIST)");

HLSStream::HLSStream() = default;

HLSStream::~HLSStream() {
    close();
}

auto HLSStream::open(const OpenParams& params) -> StreamResult<StreamInfo> {
    params_ = params;

    // 将 OpenParams 的 onMetadata 接到内部 metadataCallback_，打通 live 识别链路
    if (params.onMetadata) {
        metadataCallback_ = params.onMetadata;
    }

    auto lastSlash = params.uri.rfind('/');
    if (lastSlash != std::string::npos) {
        baseUrl_ = params.uri.substr(0, lastSlash + 1);
    }

    info_.type = StreamType::HLS;
    info_.isSeekable = true;
    info_.isLive = false;

    playlist_ = std::make_shared<MediaPlaylist>();

    state_ = StreamState::Opening;
    return info_;
}

void HLSStream::close() noexcept {
    if (request_) {
        request_->cancel();
        request_.reset();
    }

    state_ = StreamState::Idle;
    isRunning_ = false;
    isCompleted_ = false;
}

void HLSStream::start() noexcept {
    if (state_ != StreamState::Ready && state_ != StreamState::Paused &&
        state_ != StreamState::Opening) {
        return;
    }

    isPaused_ = false;
    isRunning_ = true;
    state_ = StreamState::Reading;

    request_ = std::make_shared<http::HttpRequest>();

    http::RequestInfo requestInfo;
    requestInfo.url = params_.uri;
    requestInfo.methodType = http::HttpMethodType::Get;

    auto self = shared_from_this();

    auto fetchPlaylistTask = [](std::shared_ptr<http::HttpRequest> req,
                                http::RequestInfo info,
                                std::shared_ptr<HLSStream> stream) -> Coroutine<void> {
        auto response = co_await req->asyncExecute(info);

        if (response.isOk()) {
            stream->handlePlaylistResponse(std::move(response));
        } else {
            if (stream->params_.onError) {
                stream->params_.onError(Error::NetworkError);
            }
            stream->state_ = StreamState::Error;
            stream->isRunning_ = false;
        }
    };

    (void)fetchPlaylistTask(request_, requestInfo, self);
}

void HLSStream::pause() noexcept {
    if (state_ != StreamState::Reading) {
        return;
    }

    isPaused_ = true;
    state_ = StreamState::Paused;
    isRunning_ = false;
}

auto HLSStream::seek(uint64_t position) -> StreamResult<void> {
    if (!playlist_ || playlist_->segments().empty()) {
        return std::unexpected(Error::StreamInvalidData);
    }

    double timeInSeconds = static_cast<double>(position) / 1000.0;
    const auto& segments = playlist_->segments();

    auto it = std::find_if(segments.begin(), segments.end(), [timeInSeconds](const Segment& seg) {
        return timeInSeconds >= seg.startTime && timeInSeconds < seg.startTime + seg.duration;
    });

    if (it == segments.end()) {
        return std::unexpected(Error::OutOfRange);
    }

    currentSegmentIndex_.store(it->index);
    position_ = position;

    return {};
}

void HLSStream::updateRange(Range) noexcept {}

void HLSStream::handlePlaylistResponse(http::HttpResponse&& response) {
    if (!response.body) {
        if (params_.onError) {
            params_.onError(Error::StreamInvalidData);
        }
        state_ = StreamState::Error;
        return;
    }

    auto content = response.body->toString();

    auto result = content.find("#EXT-X-STREAM-INF");
    if (result != std::string::npos) {
        auto parseResult = parseMasterPlaylist(content);
        if (!parseResult) {
            state_ = StreamState::Error;
            return;
        }

        auto variantUri = selectVariantUri(0);
        if (!variantUri) {
            state_ = StreamState::Error;
            return;
        }

        auto variantUrl = resolveUrl(*variantUri);
        mediaPlaylistUrl_ = variantUrl;
        request_ = std::make_shared<http::HttpRequest>();

        http::RequestInfo requestInfo;
        requestInfo.url = variantUrl;
        requestInfo.methodType = http::HttpMethodType::Get;

        auto self = shared_from_this();

        auto fetchVariantTask = [](std::shared_ptr<http::HttpRequest> req,
                                   http::RequestInfo info,
                                   std::shared_ptr<HLSStream> stream) -> Coroutine<void> {
            auto response = co_await req->asyncExecute(info);

            if (response.isOk()) {
                stream->handlePlaylistResponse(std::move(response));
            } else {
                if (stream->params_.onError) {
                    stream->params_.onError(Error::NetworkError);
                }
                stream->state_ = StreamState::Error;
            }
        };

        (void)fetchVariantTask(request_, requestInfo, self);
    } else {
        if (isLiveRefresh_) {
            // live 刷新：增量追加新 segment，不重置 playlist
            auto parseResult = parseMediaPlaylistIncremental(content);
            if (!parseResult) {
                state_ = StreamState::Error;
                return;
            }
            isLiveRefresh_ = false;
            downloadSegments();
        } else {
            auto parseResult = parseMediaPlaylist(content);
            if (!parseResult) {
                state_ = StreamState::Error;
                return;
            }

            if (metadataCallback_) {
                metadataCallback_(*playlist_);
            }

            // 记录 media playlist URL，live 刷新时使用
            if (mediaPlaylistUrl_.empty()) {
                mediaPlaylistUrl_ = params_.uri;
            }

            state_ = StreamState::Ready;

            downloadSegments();
        }
    }
}

auto HLSStream::parseMasterPlaylist(const std::string& content) -> StreamResult<void> {
    std::smatch match;
    std::string::const_iterator searchStart(content.cbegin());

    while (std::regex_search(searchStart, content.cend(), match, kVariantRegex)) {
        Variant variant;
        uint32_t bandwidth = 0;
        auto bwStr = match[1].str();
        auto [ptr, ec] = std::from_chars(bwStr.data(), bwStr.data() + bwStr.size(), bandwidth);
        if (ec != std::errc{}) {
            searchStart = match.suffix().first;
            continue;
        }
        variant.bandwidth = bandwidth;
        variant.uri = match[4].str();

        if (match[2].matched && match[3].matched) {
            variant.width = std::stoul(match[2].str());
            variant.height = std::stoul(match[3].str());
        }

        playlist_->addVariant(std::move(variant));
        searchStart = match.suffix().first;
    }

    if (!playlist_->hasVariants()) {
        return std::unexpected(Error::StreamInvalidData);
    }

    return {};
}

auto HLSStream::parseMediaPlaylist(const std::string& content) -> StreamResult<void> {
    if (!std::regex_search(content, kEndListRegex)) {
        playlist_->setLive(true);
        info_.isLive = true;
    }

    std::smatch match;
    std::string::const_iterator searchStart(content.cbegin());
    double currentTime = 0.0;

    while (std::regex_search(searchStart, content.cend(), match, kExtinfRegex)) {
        Segment segment;
        segment.duration = std::stod(match[1].str());
        segment.startTime = currentTime;
        segment.uri = match[3].str();

        playlist_->addSegment(std::move(segment));
        currentTime += std::stod(match[1].str());

        searchStart = match.suffix().first;
    }

    playlist_->setTotalDuration(currentTime);
    info_.totalSize = static_cast<uint64_t>(currentTime * 1000);

    if (playlist_->segments().empty()) {
        return std::unexpected(Error::StreamInvalidData);
    }

    return {};
}

auto HLSStream::selectVariantUri(uint32_t bandwidth) -> std::optional<std::string> {
    const auto& variants = playlist_->variants();
    if (variants.empty()) {
        return std::nullopt;
    }

    auto selectedIt = variants.end();
    for (auto it = variants.begin(); it != variants.end(); ++it) {
        if (bandwidth == 0 || it->bandwidth <= bandwidth) {
            if (selectedIt == variants.end() || it->bandwidth > selectedIt->bandwidth) {
                selectedIt = it;
            }
        }
    }

    if (selectedIt == variants.end()) {
        selectedIt = variants.begin();
    }

    return selectedIt->uri;
}

void HLSStream::downloadSegments() {
    if (isPaused_ || !isRunning_) {
        return;
    }

    const auto& segments = playlist_->segments();
    auto segIndex = currentSegmentIndex_.load();
    if (segIndex >= segments.size()) {
        if (playlist_->isLive()) {
            // live 流：刷新 playlist 继续拉取新分片
            refreshPlaylist();
            return;
        }
        isCompleted_ = true;
        state_ = StreamState::Completed;
        isRunning_ = false;

        if (params_.onComplete) {
            params_.onComplete();
        }
        return;
    }

    downloadSegment(segments[segIndex]);
}

void HLSStream::downloadSegment(const Segment& segment) {
    request_ = std::make_shared<http::HttpRequest>();

    auto segmentUrl = resolveUrl(segment.uri);

    http::RequestInfo requestInfo;
    requestInfo.url = segmentUrl;
    requestInfo.methodType = http::HttpMethodType::Get;

    auto self = shared_from_this();
    auto segmentIndex = segment.index;

    auto downloadTask = [](std::shared_ptr<http::HttpRequest> req,
                           http::RequestInfo info,
                           std::shared_ptr<HLSStream> stream,
                           uint32_t segIndex) -> Coroutine<void> {
        auto gen = req->streamingExecute(info);

        while (true) {
            auto chunk = co_await gen.next();
            if (!chunk.has_value()) {
                break;
            }

            if (stream->isPaused_) {
                co_return;
            }

            stream->handleSegmentChunk(std::move(chunk.value()), segIndex);
        }

        stream->currentSegmentIndex_.fetch_add(1, std::memory_order_relaxed);
        stream->downloadSegments();
    };

    (void)downloadTask(request_, requestInfo, self, segmentIndex);
}

void HLSStream::handleSegmentChunk(http::StreamChunk&& chunk, uint32_t segmentIndex) {
    if (chunk.hasError()) {
        if (params_.onError) {
            params_.onError(Error::NetworkError);
        }
        state_ = StreamState::Error;
        return;
    }

    if (chunk.hasData()) {
        auto chunkSize = chunk.data->length();
        auto buffer = std::shared_ptr<Data>(chunk.data.release());

        if (params_.onData) {
            params_.onData(DataChunk{
                .data = DataView(buffer->view()),
                .owner = std::move(buffer),
                .offset = position_,
                .segmentIndex = segmentIndex,
                .isLastChunk = chunk.isFinal,
            });
        }

        position_ += chunkSize;
    }
}

std::string HLSStream::resolveUrl(const std::string& relativeUrl) const {
    if (relativeUrl.find(":") != std::string::npos) {
        return relativeUrl;
    }

    return baseUrl_ + relativeUrl;
}

void HLSStream::refreshPlaylist() {
    if (mediaPlaylistUrl_.empty() || isPaused_ || !isRunning_) {
        return;
    }

    isLiveRefresh_ = true;
    request_ = std::make_shared<http::HttpRequest>();

    http::RequestInfo requestInfo;
    requestInfo.url = mediaPlaylistUrl_;
    requestInfo.methodType = http::HttpMethodType::Get;

    auto self = shared_from_this();

    auto fetchTask = [](std::shared_ptr<http::HttpRequest> req,
                        http::RequestInfo info,
                        std::shared_ptr<HLSStream> stream) -> Coroutine<void> {
        auto response = co_await req->asyncExecute(info);

        if (response.isOk()) {
            stream->handlePlaylistResponse(std::move(response));
        } else {
            if (stream->params_.onError) {
                stream->params_.onError(Error::NetworkError);
            }
            stream->state_ = StreamState::Error;
            stream->isRunning_ = false;
        }
    };

    (void)fetchTask(request_, requestInfo, self);
}

auto HLSStream::parseMediaPlaylistIncremental(const std::string& content) -> StreamResult<void> {
    // 已知 segment 的 URI 集合，用于去重
    std::unordered_set<std::string> knownUris;
    for (const auto& seg : playlist_->segments()) {
        knownUris.insert(seg.uri);
    }

    double currentTime = playlist_->totalDuration();
    std::smatch match;
    std::string::const_iterator searchStart(content.cbegin());
    bool hasNew = false;

    while (std::regex_search(searchStart, content.cend(), match, kExtinfRegex)) {
        std::string uri = match[3].str();
        if (knownUris.find(uri) == knownUris.end()) {
            Segment segment;
            segment.duration = std::stod(match[1].str());
            segment.startTime = currentTime;
            segment.uri = uri;
            currentTime += segment.duration;
            playlist_->addSegment(std::move(segment));
            hasNew = true;
        }
        searchStart = match.suffix().first;
    }

    if (hasNew) {
        playlist_->setTotalDuration(currentTime);
    }

    return {};
}

} // namespace slark
