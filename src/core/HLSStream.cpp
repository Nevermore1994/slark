//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#include "HLSStream.h"

#include <algorithm>
#include <chrono>
#include <unordered_set>

#include "CoroutineScheduler.hpp"
#include "DataView.hpp"
#include "Log.hpp"
#include "LogConfig.h"
#include "M3U8Parser.h"

namespace slark {

namespace {
inline uint64_t logProgressBytes() {
    return getLogConfig().progressLogBytes;
}

constexpr std::chrono::milliseconds kWaitSettleTimeout{500};
constexpr std::chrono::milliseconds kCloseWaitTimeout{5000};
}  // namespace

HLSStream::HLSStream(std::shared_ptr<Thread> ownerThread)
    : ownerThread_(std::move(ownerThread)),
      controlSession_(std::make_shared<http::Http1Session>()),
      mediaSession_(std::make_shared<http::Http1Session>()) {}

HLSStream::~HLSStream() {
    close();
}

StreamResult<StreamInfo> HLSStream::open(const OpenParams& params) {
    params_ = params;

    if (params.onMetadata) {
        metadataCallback_ = params.onMetadata;
    }

    auto lastSlash = params.uri.rfind('/');
    if (lastSlash != std::string::npos) {
        baseUrl_ = params.uri.substr(0, lastSlash + 1);
    }

    {
        std::lock_guard<std::mutex> lock(infoMutex_);
        info_.type = StreamType::HLS;
        info_.uri = params.uri;
        info_.isSeekable = true;
        info_.isLive = false;
    }

    state_ = StreamState::Opening;
    return info_;
}

void HLSStream::close() noexcept {
    LogI("[HLSStream::close] url={} pos={} state={}", params_.uri, position_.load(),
         static_cast<int>(state_.load()));
    std::lock_guard<std::mutex> lock(mutex_);
    closing_ = true;
    ++generation_;
    cancelPlaylistOp(kCloseWaitTimeout.count());
    cancelSegmentOp(kCloseWaitTimeout.count());
    controlSession_->close();
    mediaSession_->close();
    if (refreshTimerId_.isValid()) {
        ownerThread_->cancelRepeatTask(refreshTimerId_);
        refreshTimerId_ = {};
    }
    if (retryTimerId_.isValid()) {
        ownerThread_->cancelRepeatTask(retryTimerId_);
        retryTimerId_ = {};
    }
    state_ = StreamState::Idle;
    isRunning_ = false;
    isCompleted_ = false;
    isPaused_ = false;
}

void HLSStream::start() noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    if (closing_) {
        return;
    }
    if (state_ != StreamState::Ready && state_ != StreamState::Paused &&
        state_ != StreamState::Opening) {
        LogI("[HLSStream::start] skip, state={}", static_cast<int>(state_.load()));
        return;
    }

    state_ = StreamState::Reading;
    isPaused_ = false;
    isRunning_ = true;
    started_ = true;

    LogI("[HLSStream::start] url={}", params_.uri);

    if (playlist_ && !mediaPlaylistUrl_.empty()) {
        ++generation_;
        cancelSegmentOp(kWaitSettleTimeout.count());
        downloadSegments();
        return;
    }

    ++generation_;
    cancelPlaylistOp(kWaitSettleTimeout.count());

    auto request = std::make_shared<http::HttpRequest>();
    auto control = std::make_shared<StreamControl>();
    uint64_t gen = generation_;
    uint64_t opId = nextOperationId_++;

    http::RequestInfo requestInfo;
    requestInfo.url = params_.uri;
    requestInfo.methodType = http::HttpMethodType::Get;
    requestInfo.connectionMode = http::HttpConnectionMode::KeepAlive;
    requestInfo.session = controlSession_;

    auto task = fetchPlaylist(request, requestInfo, control, gen, opId);
    playlistOp_.emplace(PlaylistOp{gen, opId, request, std::move(task), std::move(control)});
    playlistOp_->task.resume();
}

void HLSStream::pause() noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    if (state_ != StreamState::Reading) {
        LogI("[HLSStream::pause] noop, state={}", static_cast<int>(state_.load()));
        return;
    }

    ++generation_;
    cancelSegmentOp(kWaitSettleTimeout.count());
    cancelPlaylistOp(kWaitSettleTimeout.count());
    controlSession_->close();
    mediaSession_->close();
    if (refreshTimerId_.isValid()) {
        ownerThread_->cancelRepeatTask(refreshTimerId_);
        refreshTimerId_ = {};
    }
    if (retryTimerId_.isValid()) {
        ownerThread_->cancelRepeatTask(retryTimerId_);
        retryTimerId_ = {};
    }

    isPaused_ = true;
    state_ = StreamState::Paused;
    isRunning_ = false;
    LogI("[HLSStream::pause] seg={} pos={}", currentSegmentIndex_.load(), position_.load());
}

StreamResult<void> HLSStream::seek(uint64_t /*position*/) {
    return std::unexpected(Error::InvalidOperation);
}

StreamResult<void> HLSStream::seekByTime(double timeSec) {
    LogI("[HLSStream::seekByTime] time={}", timeSec);
    std::lock_guard<std::mutex> lock(mutex_);
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

    ++generation_;
    cancelSegmentOp(kWaitSettleTimeout.count());
    cancelPlaylistOp(kWaitSettleTimeout.count());
    controlSession_->close();
    mediaSession_->close();
    if (retryTimerId_.isValid()) {
        ownerThread_->cancelRepeatTask(retryTimerId_);
        retryTimerId_ = {};
    }

    if (isRunning_) {
        downloadSegments();
    }
    return {};
}

void HLSStream::updateRange(Range) noexcept {}

Task<void> HLSStream::fetchPlaylist(std::shared_ptr<http::HttpRequest> request,
                                    http::RequestInfo info,
                                    std::shared_ptr<StreamControl> control,
                                    uint64_t generation,
                                    uint64_t operationId) {
    std::weak_ptr<HLSStream> weakStream = weak_from_this();
    auto response = co_await request->asyncExecute(info);
    if (!control->alive.load()) {
        co_return;
    }
    auto event = PlaylistEvent{generation, operationId, std::move(response)};
    ownerThread_->postTask([weakStream, event = std::move(event)]() mutable {
        auto s = weakStream.lock();
        if (!s) {
            return;
        }
        s->onPlaylistEvent(std::move(event));
    });
}

void HLSStream::onPlaylistEvent(PlaylistEvent event) noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    if (event.generation != generation_ || !playlistOp_ ||
        event.operationId != playlistOp_->operationId) {
        return;
    }
    playlistOp_.reset();
    handlePlaylistResponse(std::move(event.response));
}

void HLSStream::handlePlaylistResponse(http::HttpResponse&& response) {
    LogI("[HLSStream::handlePlaylistResponse] received: status={} bodySize={}",
         static_cast<int>(response.statusCode), response.body ? response.body->size() : 0);

    if (!response.isSuccess() || !response.body) {
        reportHttpError(response);
        return;
    }

    auto content = response.body->toString();

    if (!parser_) {
        parser_ = std::make_unique<M3U8Parser>(baseUrl_);
    }

    if (!parser_->parse(content)) {
        LogE("[HLSStream::handlePlaylistResponse] M3U8 parse failed: bodySize={}", content.size());
        if (params_.onError) {
            params_.onError(Error::StreamInvalidData, 0);
        }
        state_ = StreamState::Error;
        isRunning_ = false;
        return;
    }

    playlist_ = parser_->playlist();

    if (parser_->isMasterPlaylist()) {
        LogI("[HLSStream::handlePlaylistResponse] master playlist parsed, selecting variant");
        auto variantUri = selectVariantUri(0);
        if (!variantUri) {
            LogE("[HLSStream::handlePlaylistResponse] no variant available");
            state_ = StreamState::Error;
            isRunning_ = false;
            return;
        }

        auto variantUrl = resolveUrl(*variantUri);
        mediaPlaylistUrl_ = variantUrl;
        LogI("[HLSStream::handlePlaylistResponse] variant selected: {}", variantUrl);

        parser_.reset();
        cancelPlaylistOp(kWaitSettleTimeout.count());
        ++generation_;

        auto request = std::make_shared<http::HttpRequest>();
        auto control = std::make_shared<StreamControl>();
        uint64_t gen = generation_;
        uint64_t opId = nextOperationId_++;

        http::RequestInfo requestInfo;
        requestInfo.url = variantUrl;
        requestInfo.methodType = http::HttpMethodType::Get;
        requestInfo.connectionMode = http::HttpConnectionMode::KeepAlive;
        requestInfo.session = controlSession_;

        auto task = fetchPlaylist(request, requestInfo, control, gen, opId);
        playlistOp_.emplace(PlaylistOp{gen, opId, request, std::move(task), std::move(control)});
        playlistOp_->task.resume();
    } else {
        {
            std::lock_guard<std::mutex> lock(infoMutex_);
            info_.isLive = playlist_->isLive();
            info_.isSeekable = !playlist_->isLive();
            info_.totalSize = static_cast<uint64_t>(playlist_->totalDuration() * 1000);
        }

        LogI("[HLSStream::handlePlaylistResponse] media playlist parsed: segments={} isLive={} "
             "totalDuration={}",
             playlist_->segments().size(), info_.isLive, playlist_->totalDuration());

        if (metadataCallback_) {
            metadataCallback_(std::static_pointer_cast<void>(playlist_));
        }

        if (mediaPlaylistUrl_.empty()) {
            mediaPlaylistUrl_ = params_.uri;
        }

        if (info_.isLive) {
            refreshTimerId_ =
                ownerThread_->postTask(std::chrono::seconds(3), [weak = weak_from_this()]() {
                    auto self = weak.lock();
                    if (!self) {
                        return;
                    }
                    self->refreshPlaylist();
                });
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
            return;
        }
        LogI("[HLSStream::downloadSegments] all segments done, total={}", segments.size());
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
    if (!playlist_ || segIndex >= playlist_->segments().size()) {
        return;
    }

    const auto& segment = playlist_->segments()[segIndex];
    LogI("[HLSStream::downloadSegment] start: segIndex={} url={}", segIndex,
         resolveUrl(segment.uri));

    auto request = std::make_shared<http::HttpRequest>();
    auto control = std::make_shared<StreamControl>();
    uint64_t gen = generation_;
    uint64_t opId = nextOperationId_++;

    http::RequestInfo requestInfo;
    requestInfo.url = resolveUrl(segment.uri);
    requestInfo.methodType = http::HttpMethodType::Get;
    requestInfo.connectionMode = http::HttpConnectionMode::KeepAlive;
    requestInfo.session = mediaSession_;

    std::weak_ptr<HLSStream> weakThis = shared_from_this();
    std::weak_ptr<StreamControl> weakControl = control;

    auto downloadTask = [](std::shared_ptr<http::HttpRequest> req, http::RequestInfo info,
                           std::weak_ptr<HLSStream> weakStream,
                           std::weak_ptr<StreamControl> weakControl,
                           std::shared_ptr<Thread> ownerThread, uint32_t segIdx,
                           uint64_t generation, uint64_t operationId) -> Task<void> {
        auto gen = req->streamingExecute(info);
        while (true) {
            auto chunk = co_await gen.next();
            if (!chunk.has_value()) {
                break;
            }
            auto control = weakControl.lock();
            if (!control || !control->alive.load()) {
                co_return;
            }
            auto stream = weakStream.lock();
            if (!stream) {
                co_return;
            }
            auto event =
                HLSStream::SegmentEvent{generation, operationId, segIdx, std::move(*chunk)};
            ownerThread->postTask([weakStream, event = std::move(event)]() mutable {
                auto s = weakStream.lock();
                if (!s) {
                    return;
                }
                s->onSegmentEvent(std::move(event));
            });
        }
    };

    auto task = downloadTask(request, requestInfo, weakThis, weakControl, ownerThread_, segIndex,
                             gen, opId);
    segmentOp_.emplace(
        SegmentOp{gen, opId, segIndex, request, std::move(task), std::move(control)});
    segmentOp_->task.resume();
}

void HLSStream::onSegmentEvent(SegmentEvent event) noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    if (event.generation != generation_ || !segmentOp_ ||
        event.operationId != segmentOp_->operationId) {
        return;
    }

    auto& chunk = event.chunk;
    if (chunk.hasError()) {
        lastErrorOriginCode_.store(chunk.error.originErrorCode, std::memory_order_release);
        LogE("[HLSStream::onSegmentEvent] segment {} error: retCode={} originCode={}",
             event.segIndex, static_cast<int>(chunk.error.retCode), chunk.error.originErrorCode);
        if (chunk.error.retCode == Error::Cancelled) {
            return;
        }
        cancelSegmentOp(kWaitSettleTimeout.count());
        scheduleRetry(event.segIndex);
        return;
    }

    handleSegmentChunk(std::move(event.chunk), event.segIndex);
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

        auto pos = position_.load();
        if (pos / logProgressBytes() != (pos - chunkSize) / logProgressBytes()) {
            LogI("[HLSStream::handleSegmentChunk] progress: segIndex={} pos={} chunk={}",
                 segmentIndex, pos, chunkSize);
        }
    }

    if (chunk.isFinal) {
        cancelSegmentOp(kWaitSettleTimeout.count());
        retryCount_ = 0;
        currentSegmentIndex_.fetch_add(1, std::memory_order_relaxed);
        LogI("[HLSStream::handleSegmentChunk] segment {} done, next={}", segmentIndex,
             currentSegmentIndex_.load());
        downloadSegments();
    }
}

void HLSStream::scheduleRetry(uint32_t segIndex) {
    if (closing_ || isPaused_) {
        return;
    }
    const auto& segments = playlist_->segments();
    if (segIndex >= segments.size()) {
        return;
    }
    std::chrono::milliseconds delay{0};
    if (retryCount_ >= 3) {
        LogE("[HLSStream::scheduleRetry] segment {} retry exhausted", segIndex);
        reportSegmentError(segIndex, lastErrorOriginCode_.load(std::memory_order_acquire));
        retryCount_ = 0;
        return;
    }
    if (retryCount_ == 1) {
        delay = std::chrono::milliseconds(500);
    } else if (retryCount_ == 2) {
        delay = std::chrono::seconds(1);
    }
    ++retryCount_;
    LogW("[HLSStream::scheduleRetry] segment {} retrying in {}ms (count={})", segIndex,
         delay.count(), retryCount_);
    if (retryTimerId_.isValid()) {
        ownerThread_->cancelRepeatTask(retryTimerId_);
    }
    retryTimerId_ = ownerThread_->postTask(delay, [weak = weak_from_this(), segIndex]() {
        auto self = weak.lock();
        if (!self) {
            return;
        }
        std::lock_guard<std::mutex> lock(self->mutex_);
        if (self->closing_ || self->isPaused_ || !self->isRunning_) {
            return;
        }
        self->retryTimerId_ = {};
        self->downloadSegment(segIndex);
    });
}

void HLSStream::refreshPlaylist() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (mediaPlaylistUrl_.empty() || isPaused_ || !isRunning_ || closing_) {
        return;
    }

    LogI("[HLSStream::refreshPlaylist] url={}", mediaPlaylistUrl_);
    if (playlistOp_) {
        return;
    }

    auto request = std::make_shared<http::HttpRequest>();
    auto control = std::make_shared<StreamControl>();
    uint64_t gen = generation_;
    uint64_t opId = nextOperationId_++;

    http::RequestInfo requestInfo;
    requestInfo.url = mediaPlaylistUrl_;
    requestInfo.methodType = http::HttpMethodType::Get;
    requestInfo.connectionMode = http::HttpConnectionMode::KeepAlive;
    requestInfo.session = controlSession_;

    auto task = fetchPlaylist(request, requestInfo, control, gen, opId);
    playlistOp_.emplace(PlaylistOp{gen, opId, request, std::move(task), std::move(control)});
    playlistOp_->task.resume();
}

void HLSStream::reportHttpError(const http::HttpResponse& response) {
    if (!params_.onError) {
        state_ = StreamState::Error;
        isRunning_ = false;
        return;
    }
    Error err = Error::NetworkError;
    switch (response.error.retCode) {
        case slark::Error::ConnectionTimeout:
        case slark::Error::HttpError:
        case slark::Error::Cancelled:
        case slark::Error::DnsResolveFailed:
        case slark::Error::ConnectionFailed:
            err = response.error.retCode;
            break;
        default:
            break;
    }
    LogE("[HLSStream::reportHttpError] err={} originCode={}", static_cast<int>(err),
         response.error.originErrorCode);
    params_.onError(err, response.error.originErrorCode);
    state_ = StreamState::Error;
    isRunning_ = false;
}

void HLSStream::reportSegmentError(uint32_t segIndex, int32_t originCode) {
    lastErrorOriginCode_.store(originCode, std::memory_order_release);
    LogE("[HLSStream::reportSegmentError] segIndex={} originCode={}", segIndex, originCode);
    if (params_.onError) {
        params_.onError(Error::NetworkError, originCode);
    }
    state_ = StreamState::Error;
    isRunning_ = false;
}

std::string HLSStream::resolveUrl(const std::string& relativeUrl) const {
    return M3U8Parser::resolveUrl(baseUrl_, relativeUrl);
}

void HLSStream::cancelPlaylistOp(uint64_t timeoutMs) {
    if (!playlistOp_) {
        return;
    }
    playlistOp_->request->cancel();
    playlistOp_->control->alive = false;

    auto opTask = std::move(playlistOp_->task);
    playlistOp_.reset();
    mutex_.unlock();

    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    while (!opTask.isDone()) {
        if (std::chrono::steady_clock::now() >= deadline) {
            LogW("[HLSStream::cancelPlaylistOp] task not settled within {}ms", timeoutMs);
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    mutex_.lock();
}

void HLSStream::cancelSegmentOp(uint64_t timeoutMs) {
    if (!segmentOp_) {
        return;
    }
    segmentOp_->request->cancel();
    segmentOp_->control->alive = false;

    auto opTask = std::move(segmentOp_->task);
    segmentOp_.reset();
    mutex_.unlock();

    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    while (!opTask.isDone()) {
        if (std::chrono::steady_clock::now() >= deadline) {
            LogW("[HLSStream::cancelSegmentOp] task not settled within {}ms", timeoutMs);
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    mutex_.lock();
}

}  // namespace slark
