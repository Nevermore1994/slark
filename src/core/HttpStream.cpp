//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#include "HttpStream.h"

#include "Coroutine.hpp"
#include "DataView.hpp"

namespace slark {

HttpStream::HttpStream() = default;

HttpStream::~HttpStream() {
    close();
}

auto HttpStream::open(const OpenParams& params) -> StreamResult<StreamInfo> {
    params_ = params;
    readRange_ = params.range;

    info_.type = StreamType::Http;
    info_.uri = params.uri;
    info_.isSeekable = false;
    info_.isLive = false;

    state_ = StreamState::Ready;
    return info_;
}

void HttpStream::close() noexcept {
    if (request_) {
        request_->cancel();
        request_.reset();
    }

    state_ = StreamState::Idle;
    isRunning_ = false;
    isCompleted_ = false;
}

void HttpStream::start() noexcept {
    if (state_ != StreamState::Ready && state_ != StreamState::Paused) {
        return;
    }

    isPaused_ = false;
    isRunning_ = true;
    state_ = StreamState::Reading;

    request_ = std::make_shared<http::HttpRequest>();

    http::RequestInfo requestInfo;
    requestInfo.url = params_.uri;
    requestInfo.methodType = http::HttpMethodType::Get;

    if (readRange_.isValid()) {
        requestInfo.headers["Range"] = readRange_.toHeaderString();
    }

    auto self = request_;
    std::weak_ptr<HttpStream> weakThis = shared_from_this();

    auto streamTask = [](std::shared_ptr<http::HttpRequest> req,
                         http::RequestInfo info,
                         std::weak_ptr<HttpStream> weakStream) -> Coroutine<void> {
        auto gen = req->streamingExecute(info);

        while (true) {
            auto chunk = co_await gen.next();
            if (!chunk.has_value()) {
                break;
            }

            auto stream = weakStream.lock();
            if (!stream) {
                co_return;
            }

            stream->handleChunk(std::move(chunk.value()));

            if (stream->isPaused_ || stream->isCompleted_) {
                co_return;
            }
        }

        if (auto stream = weakStream.lock()) {
            stream->isCompleted_ = true;
            stream->state_ = StreamState::Completed;
            stream->isRunning_ = false;

            if (stream->params_.onComplete) {
                stream->params_.onComplete();
            }
        }
    };

    (void)streamTask(self, requestInfo, weakThis);
}

void HttpStream::pause() noexcept {
    if (state_ != StreamState::Reading) {
        return;
    }

    isPaused_ = true;
    state_ = StreamState::Paused;
    isRunning_ = false;
}

auto HttpStream::seek(uint64_t position) -> StreamResult<void> {
    if (!acceptRanges_) {
        return std::unexpected(Error::StreamNotReady);
    }

    readRange_ = Range(position);
    position_ = position;

    if (request_) {
        request_->cancel();
        request_.reset();
    }

    isCompleted_ = false;
    state_ = StreamState::Ready;
    start();

    return {};
}

void HttpStream::updateRange(Range range) noexcept {
    readRange_ = range;
}

void HttpStream::handleChunk(http::StreamChunk&& chunk) noexcept {
    if (chunk.hasError()) {
        if (params_.onError) {
            params_.onError(Error::NetworkError);
        }
        isRunning_ = false;
        state_ = StreamState::Error;
        return;
    }

    if (chunk.statusCode != http::HttpStatusCode::Unknown) {
        auto it = chunk.headers.find("Accept-Ranges");
        if (it != chunk.headers.end() && it->second == "bytes") {
            acceptRanges_ = true;
            info_.isSeekable = true;
        }

        auto lengthIt = chunk.headers.find("Content-Length");
        if (lengthIt != chunk.headers.end()) {
            auto lenResult = StringUtil::fromChars<uint64_t>(lengthIt->second);
            if (lenResult) {
                contentLength_ = *lenResult;
                info_.totalSize = contentLength_;
            }
        }

        auto rangeIt = chunk.headers.find("Content-Range");
        if (rangeIt != chunk.headers.end()) {
            const auto& rangeStr = rangeIt->second;
            auto slashPos = rangeStr.find('/');
            if (slashPos != std::string::npos) {
                auto totalStr = rangeStr.substr(slashPos + 1);
                auto totalResult = StringUtil::fromChars<uint64_t>(totalStr);
                if (totalResult) {
                    info_.totalSize = *totalResult;
                }
            }
        }

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
                .segmentIndex = 0,
                .isLastChunk = chunk.isFinal,
            });
        }

        position_ += chunkSize;
    }

    if (chunk.isFinal) {
        isCompleted_ = true;
        state_ = StreamState::Completed;
        isRunning_ = false;

        if (params_.onComplete) {
            params_.onComplete();
        }
    }
}

void HttpStream::handleError(const http::ErrorInfo& /*error*/) noexcept {
    if (params_.onError) {
        params_.onError(Error::NetworkError);
    }

    state_ = StreamState::Error;
    isRunning_ = false;
}

} // namespace slark
