//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#include "HLSStream.h"

#include "Coroutine.hpp"
#include "DataView.hpp"
#include "HttpUtil.hpp"
#include "Log.hpp"
#include "M3U8Parser.h"

#include <algorithm>
#include <unordered_set>

namespace slark {

HLSStream::HLSStream() = default;

HLSStream::~HLSStream() {
    close();
}

StreamResult<StreamInfo> HLSStream::open(const OpenParams& params) {
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
    info_.uri = params.uri;
    info_.isSeekable = true;
    info_.isLive = false;

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

StreamResult<void> HLSStream::seek(uint64_t /*position*/) {
    // HLS \u4f7f\u7528 seekByTime \u8fdb\u884c\u65f6\u95f4\u8bed\u4e49\u7684
    // seek\uff0c\u4e0d\u652f\u6301 byte offset seek
    return std::unexpected(Error::InvalidOperation);
}

StreamResult<void> HLSStream::seekByTime(double timeSec) {
    if (!playlist_ || playlist_->segments().empty()) {
        return std::unexpected(Error::StreamInvalidData);
    }
    if (playlist_->isLive()) {
        return std::unexpected(Error::InvalidOperation);
    }
    auto segOpt = playlist_->findSegmentByTime(timeSec);
    if (!segOpt) {
        return std::unexpected(Error::OutOfRange);
    }
    currentSegmentIndex_.store(*segOpt);
    position_ = 0;
    // \u53d6\u6d88\u5f53\u524d segment \u4e0b\u8f7d\uff0c\u91cd\u65b0\u62c9\u53d6\u76ee\u6807
    // segment
    if (request_) {
        request_->cancel();
        request_.reset();
    }
    if (isRunning_) {
        downloadSegments();
    }
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

    if (!parser_) {
        parser_ = std::make_unique<M3U8Parser>(baseUrl_);
    }

    if (!parser_->parse(content)) {
        if (params_.onError) {
            params_.onError(Error::StreamInvalidData);
        }
        state_ = StreamState::Error;
        return;
    }

    playlist_ = parser_->playlist();

    if (parser_->isMasterPlaylist()) {
        auto variantUri = selectVariantUri(0);
        if (!variantUri) {
            state_ = StreamState::Error;
            return;
        }

        auto variantUrl = resolveUrl(*variantUri);
        mediaPlaylistUrl_ = variantUrl;

        // 创建新的 parser 解析 media playlist
        parser_.reset();
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
        info_.isLive = playlist_->isLive();
        info_.isSeekable = !playlist_->isLive();
        info_.totalSize = static_cast<uint64_t>(playlist_->totalDuration() * 1000);

        if (metadataCallback_) {
            metadataCallback_(std::static_pointer_cast<void>(playlist_));
        }

        if (mediaPlaylistUrl_.empty()) {
            mediaPlaylistUrl_ = params_.uri;
        }

        state_ = StreamState::Ready;
        downloadSegments();
    }
}

std::optional<std::string> HLSStream::selectVariantUri(uint32_t bandwidth) {
    if (!playlist_ || !playlist_->hasVariants()) {
        return std::nullopt;
    }

    const auto& variants = playlist_->variants();
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
    if (isPaused_ || !isRunning_ || !playlist_) {
        return;
    }

    const auto& segments = playlist_->segments();
    auto segIndex = currentSegmentIndex_.load();
    if (segIndex >= segments.size()) {
        if (playlist_->isLive()) {
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

    downloadSegment(segIndex);
}

void HLSStream::downloadSegment(uint32_t segIndex) {
    constexpr int32_t kMaxRetry = 3;
    downloadSegmentWithRetry(segIndex, kMaxRetry);
}

void HLSStream::downloadSegmentWithRetry(uint32_t segIndex, int32_t retryLeft) {
    if (!playlist_ || segIndex >= playlist_->segments().size()) {
        return;
    }

    const auto& segment = playlist_->segments()[segIndex];
    request_ = std::make_shared<http::HttpRequest>();

    http::RequestInfo requestInfo;
    requestInfo.url = resolveUrl(segment.uri);
    requestInfo.methodType = http::HttpMethodType::Get;

    auto self = shared_from_this();

    auto downloadTask = [](std::shared_ptr<http::HttpRequest> req,
                           http::RequestInfo info,
                           std::shared_ptr<HLSStream> stream,
                           uint32_t segIdx,
                           int32_t left) -> Coroutine<void> {
        auto gen = req->streamingExecute(info);
        bool hasError = false;

        while (true) {
            auto chunk = co_await gen.next();
            if (!chunk.has_value()) {
                break;
            }

            if (stream->isPaused_) {
                co_return;
            }

            if (chunk->hasError()) {
                hasError = true;
                break;
            }

            stream->handleSegmentChunk(std::move(chunk.value()), segIdx);
        }

        if (hasError) {
            if (left > 0) {
                LogW("HLSStream: segment {} download failed, retrying ({} left)", segIdx, left);
                stream->downloadSegmentWithRetry(segIdx, left - 1);
            } else {
                LogE("HLSStream: segment {} download failed after all retries", segIdx);
                if (stream->params_.onError) {
                    stream->params_.onError(Error::NetworkError);
                }
                stream->state_ = StreamState::Error;
            }
            co_return;
        }

        stream->currentSegmentIndex_.fetch_add(1, std::memory_order_relaxed);
        stream->downloadSegments();
    };

    (void)downloadTask(request_, requestInfo, self, segIndex, retryLeft);
}

void HLSStream::handleSegmentChunk(http::StreamChunk&& chunk, uint32_t segmentIndex) {
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
    return M3U8Parser::resolveUrl(baseUrl_, relativeUrl);
}

void HLSStream::refreshPlaylist() {
    if (mediaPlaylistUrl_.empty() || isPaused_ || !isRunning_) {
        return;
    }

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

} // namespace slark
