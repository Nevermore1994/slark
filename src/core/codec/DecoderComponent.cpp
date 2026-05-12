//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#include "DecoderComponent.h"

#include "DecoderConfig.h"
#include "Log.hpp"
#include "Util.h"

namespace slark {

DecoderComponent::DecoderComponent(DecoderReceiveFunc callback)
    : callback_(std::move(callback)), decodeWorker_(Thread::create("decoder")) {}

DecoderComponent::~DecoderComponent() {
    close();
}

bool DecoderComponent::open(DecoderType type,
                            const std::shared_ptr<DecoderConfig>& config,
                            DecoderOpenCallback onOpened) noexcept {
    if (isOpened_) {
        close();
    }
    isVideo_ = IDecoder::isVideoDecoder(type);
    auto self = shared_from_this();
    // 延长生命周期，防止 detach 后 use-after-free
    decodeWorker_->schedule([type, config, onOpened = std::move(onOpened), self]() {
        auto decoder = DecoderManager::shareInstance().create(type);
        if (!decoder) {
            LogE("create {} decoder failed", self->isVideo_ ? "video" : "audio");
            if (onOpened) {
                onOpened({false, Error::CodecUnsupportedFormat, "create decoder failed"});
            }
            return;
        }
        decoder->setReceiveFunc([weak = std::weak_ptr(self)](AVFrameRefPtr frame) {
            if (auto s = weak.lock()) {
                s->callback_(std::move(frame));
            }
        });
        if (!decoder->open(config)) {
            LogE("open {} decoder failed", self->isVideo_ ? "video" : "audio");
            if (onOpened) {
                onOpened({false, Error::CodecConfigInvalid, "open decoder failed"});
            }
            return;
        }
        self->decoder_.withLock([&decoder](auto& coder) {
            coder = std::move(decoder);
        });
        self->isOpened_ = true;
        auto workerName = Util::genRandomName(self->isVideo_ ? std::string("videoDecode_")
                                                             : std::string("audioDecode_"));
        self->decodeWorker_->setThreadName(workerName);
        LogI("open {} decoder success", self->isVideo_ ? "video" : "audio");
        if (onOpened) {
            onOpened({true, Error::Success, {}});
        }
    });
    return true;
}

void DecoderComponent::close() noexcept {
    if (!isOpened_) {
        return;
    }
    isOpened_ = false;
    decodeWorker_->pause();
    // 停止调度，避免 repeat task 继续执行
    decoder_.withLock([](auto& coder) {
        if (coder) {
            coder->close();
            coder.reset();
        }
    });
    cond_.notify_all();
}

void DecoderComponent::send(AVFramePtr packet) noexcept {
    {
        std::lock_guard lock(mutex_);
        pendingDecodeQueue_.emplace_back(std::move(packet));
    }
    cond_.notify_one();
    decodeWorker_->schedule([this]() {
        pushFrameDecode();
    });
}

void DecoderComponent::flush() noexcept {
    {
        std::lock_guard lock(mutex_);
        pendingDecodeQueue_.clear();
    }
    decoder_.withLock([](auto& coder) {
        if (coder) {
            coder->flush();
        }
    });
    isInputCompleted_ = false;
    LogI("flushed");
}

void DecoderComponent::start() noexcept {
    decodeWorker_->start();
    decodeWorker_->schedule([this]() {
        pushFrameDecode();
    });
}

void DecoderComponent::pause() noexcept {
    decodeWorker_->pause();
}

void DecoderComponent::setInputCompleted() noexcept {
    isInputCompleted_ = true;
    decodeWorker_->schedule([this]() {
        pushFrameDecode();
    });
}

bool DecoderComponent::isInputCompleted() const noexcept {
    return isInputCompleted_;
}

void DecoderComponent::setErrorCallback(ErrorCallback cb) noexcept {
    errorCallback_ = std::move(cb);
}

bool DecoderComponent::empty() const noexcept {
    std::lock_guard lock(mutex_);
    return pendingDecodeQueue_.empty();
}

double DecoderComponent::pendingMaxPts() const noexcept {
    std::lock_guard lock(mutex_);
    if (pendingDecodeQueue_.empty()) {
        return -1.0;
    }
    return pendingDecodeQueue_.back()->ptsTime();
}

bool DecoderComponent::isRunning() const noexcept {
    return decodeWorker_->isRunning();
}

bool DecoderComponent::isDecodeCompleted() const noexcept {
    bool completed = false;
    decoder_.withLock([&completed](const auto& coder) {
        if (coder) {
            completed = coder->isCompleted();
        }
    });
    return completed;
}

AVFrameRefPtr DecoderComponent::requestDecodeFrame(bool isBlocking) noexcept {
    if (isBlocking) {
        std::unique_lock lock(mutex_);
        cond_.wait(lock, [this] {
            return !pendingDecodeQueue_.empty() || !isOpened_;
        });
        if (!isOpened_ || pendingDecodeQueue_.empty()) {
            return nullptr;
        }
        auto frame = pendingDecodeQueue_.front();
        pendingDecodeQueue_.pop_front();
        return frame;
    } else {
        std::lock_guard lock(mutex_);
        if (pendingDecodeQueue_.empty()) {
            return nullptr;
        }
        auto frame = pendingDecodeQueue_.front();
        pendingDecodeQueue_.pop_front();
        return frame;
    }
}

AVFrameRefPtr DecoderComponent::peekDecodeFrame() noexcept {
    std::lock_guard lock(mutex_);
    if (pendingDecodeQueue_.empty()) {
        return nullptr;
    }
    return pendingDecodeQueue_.front();
}

AVFrameRefPtr DecoderComponent::buildEOSFrame(bool isVideo) noexcept {
    auto frame = std::make_shared<AVFrame>(isVideo ? AVFrameType::Video : AVFrameType::Audio);
    frame->isEndOfStream = true;
    if (isVideo) {
        frame->info = VideoFrameInfo{};
    } else {
        frame->info = AudioFrameInfo{};
    }
    return frame;
}

void DecoderComponent::pushFrameDecode() {
    if (!isOpened_) {
        return;
    }
    // 缩小锁粒度：只在获取 decoder 指针时持锁
    auto decoder = decoder_.withLock([](auto& coder) -> std::shared_ptr<IDecoder> {
        return coder;
    });
    if (!decoder || !decoder->isOpen() || decoder->isCompleted()) {
        return;
    }
    // 输入结束，推送 EOS 帧通知 decoder 完成
    if (isInputCompleted_ && !eosInjected_) {
        eosInjected_ = true;
        auto eosFrame = buildEOSFrame(decoder->isVideo());
        decoder->decode(eosFrame);
        LogI("push eos frame, {}", isVideo_ ? "video" : "audio");
        return;
    }
    // 循环消费队列，直到为空或解码完成
    while (isOpened_) {
        AVFrameRefPtr frame;
        {
            std::lock_guard lock(mutex_);
            if (pendingDecodeQueue_.empty()) {
                break;
            }
            frame = pendingDecodeQueue_.front();
            if (!frame) {
                break;
            }
        }
        auto res = decoder->decode(frame);
        if (res != Error::Success && res != Error::CodecAgain) {
            LogE("decode error:{}", toString(res));
            // 连续失败计数
            auto failCount = consecutiveFailCount_.fetch_add(1, std::memory_order_relaxed) + 1;
            if (failCount >= kMaxConsecutiveFailCount && errorCallback_) {
                LogE("consecutive decode failure count({}) exceeds threshold, fire DecodeError",
                     failCount);
                errorCallback_(res, isVideo_);
                consecutiveFailCount_.store(0, std::memory_order_relaxed);
            }
            // 丢弃损坏帧，避免无限重试
            {
                std::lock_guard lock(mutex_);
                pendingDecodeQueue_.pop_front();
            }
            break;
        }
        consecutiveFailCount_.store(0, std::memory_order_relaxed);
        {
            std::lock_guard lock(mutex_);
            pendingDecodeQueue_.pop_front();
        }
        if (!frame->isFastPush) {
            break;
        }
    }
    if (decoder->isCompleted()) {
        LogI("{} decode completed", isVideo_ ? "video" : "audio");
    }
}

} // namespace slark
