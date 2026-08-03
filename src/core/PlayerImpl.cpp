//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#include "PlayerImpl.h"

#include <algorithm>
#include <chrono>
#include <optional>
#include <utility>

#include "Base.h"
#include "DecoderConfig.h"
#include "GLContextManager.h"
#include "HttpStream.h"
#include "Log.hpp"
#include "MediaUtil.h"
#include "PlayerStateMachine.h"
#include "Random.hpp"
#include "StringUtil.h"
#include "VideoInfo.h"

namespace slark {

namespace {
static constexpr double kSeekThreshold = 0.05;

BackpressureConfig buildBackpressureConfig(StreamType type, bool isLive) {
    if (isLive || type == StreamType::HLS) {
        return BackpressureConfig::forLive();
    }
    if (type == StreamType::Http) {
        return BackpressureConfig::forNetwork();
    }
    return BackpressureConfig::forLocal();
}
}  // namespace

Player::Impl::Impl(std::unique_ptr<PlayerParams> params)
    : playerId_(Random::uuid()), notifyCenter_(playerId_) {
    SAssert(params != nullptr, "PlayerParams must not be null");
    GLContextManager::shareInstance().addMainContext(playerId_, params->mainGLContext);
    params_.withWriteLock([&params](auto& p) {
        p = std::move(params);
    });
    LogI("PlayerImpl created, id:{}", playerId_);
}

Player::Impl::~Impl() {
    isStopped_ = true;
    if (ownerThread_) {
        // 不变量：ownerThread_ 生命周期严格覆盖其 worker 函数。
        // 必须在外部线程析构，禁止 worker 自销毁 → 见下方 assert。
        SAssert(ownerThread_->getId() != std::this_thread::get_id(),
                "~Impl must not run on ownerThread_");
        ownerThread_->stop();
        ownerThread_->join();  // 同步等 worker 退出，杜绝并发 reset/UAF
        forceCleanup();        // 此时无并发，清理安全
        ownerThread_.reset();
    }
    GLContextManager::shareInstance().removeMainContext(playerId_);
    LogI("PlayerImpl destroyed, id:{}", playerId_);
}

void Player::Impl::forceCleanup() noexcept {
    LogI("force cleanup start");
    {
        std::lock_guard<std::mutex> lock(releaseMutex_);
        isReleased_ = true;
    }
    cond_.notify_all();
    if (ownerThread_) {
        ownerThread_->cancelRepeatTask(fastTickTaskId_);
        ownerThread_->cancelRepeatTask(playedTimeTaskId_);
        ownerThread_->cancelRepeatTask(cacheTickTaskId_);
    }
    mediaSourceManager_.close();
    dataPacketSender_.reset();
    if (pipeline_) {
        pipeline_->teardown();
        pipeline_.reset();
    }
    demuxerComponent_.reset();
    notifyCenter_.resetNotifyState();
    stats_.reset();
    LogI("force cleanup end");
}

void Player::Impl::seek(double time, bool isAccurate) noexcept {
    if (!sender_) {
        LogE("error!! not init");
        return;
    }
    if (isAccurate) {
        auto ptr = buildEvent(EventType::Seek);
        ptr->data = PlayerSeekRequest{.isAccurate = true, .seekTime = time};
        sender_->post(std::move(ptr));
        ownerThread_->start();
        LogI("receive accurate seek:{}, isAccurate:{}", time, isAccurate);
        return;
    }
    // 拖拽 seek：写 pendingSeekTarget_，由 processPendingSeek 消费
    // 不更新 pendingSeekTimeUs_（debounce 只在第一次触发时有用，后续由 seek in progress 控制）
    pendingSeekTarget_.store(time, std::memory_order_release);
    ownerThread_->start();
    LogI("receive seek:{}, isAccurate:{}", time, isAccurate);
}

void Player::Impl::updateState(PlayerState state) noexcept {
    if (!sender_) {
        LogE("error!! not init");
        return;
    }
    sender_->post(buildEvent(state));
    ownerThread_->start();
}

void Player::Impl::addObserver(IPlayerObserverPtr observer) noexcept {
    notifyCenter_.addObserver(std::move(observer));
}

void Player::Impl::removeObserver() noexcept {
    notifyCenter_.removeObserver();
}

void Player::Impl::init() noexcept {
    using namespace std::chrono_literals;
    setState(PlayerState::Initializing);
    auto [sp, rp] = Channel<EventPtr>::create();
    sender_ = std::move(sp);
    receiver_ = std::move(rp);

    // ownerThread_ 在 setupDataProvider 之前创建，setupDataProvider 通过 setOwnerThread 注入
    ownerThread_ = Thread::create("playerThread");

    if (!setupDataProvider()) {
        LogE("setupIOHandler failed.");
        return;
    }

    if (!createDemuxerComponent()) {
        LogE("create demuxer component failed.");
        setState(PlayerState::Error);
        return;
    }
    buildPipeline();

    PlayerSetting setting;
    params_.withReadLock([&setting](auto& p) {
        if (p) {
            setting = p->setting;
        }
    });

    fastTickTaskId_ = ownerThread_->postRepeatTask(
        std::chrono::milliseconds(setting.playerThreadTickMs), [this]() {
            process();
            if (isStopped_) {
                return;
            }
            if (auto pipeline = pipeline_) {
                pipeline->onFastTick();
            }
        });
    playedTimeTaskId_ =
        ownerThread_->postRepeatTask(std::chrono::milliseconds(setting.playedTimeTickMs), [this]() {
            if (isStopped_) {
                return;
            }
            const auto st = state();
            if (st == PlayerState::Playing) {
                notifyPlayedTime();
            }
        });
    cacheTickTaskId_ =
        ownerThread_->postRepeatTask(std::chrono::milliseconds(setting.cacheTickMs), [this]() {
            if (isStopped_) {
                return;
            }
            notifyCacheTime();
        });

    mediaSourceManager_.start();
    ownerThread_->start();
    LogI("player Initializing start.");
}

bool Player::Impl::setupDataProvider() noexcept {
    std::string path;
    params_.withReadLock([&](auto& params) {
        path = params->item.path;
    });

    if (path.empty()) {
        LogE("error, player param invalid.");
        setState(PlayerState::Error);
        notifyPlayerEvent(PlayerEvent::OnError,
                          std::to_string(static_cast<uint32_t>(Error::IOError)));
        return false;
    }

    auto weak = weak_from_this();

    auto onData = [weak](DataPacket data) {
        auto self = weak.lock();
        if (!self || self->isStopped_ || !self->dataPacketSender_) {
            return;
        }
        self->dataPacketSender_->post(std::move(data));
    };

    auto onError = [weak, path](Error err, int32_t originErrorCode) {
        auto self = weak.lock();
        if (!self || self->isStopped_) {
            return;
        }
        std::string host;
        if (auto* httpStream = dynamic_cast<HttpStream*>(self->mediaSourceManager_.rawStream())) {
            host = httpStream->host();
        }
        if (host.empty()) {
            host = StringUtil::extractHostFromUri(path);
        }
        self->notifyPlayerEvent(PlayerEvent::OnError,
                                self->formatErrorValue(err, originErrorCode, host));
        // 切 Error 会 doStop() 销毁 pipeline_，投递到 ownerThread 串行执行避免并发 UAF
        if (self->sender_) {
            self->sender_->post(buildEvent(EventType::ReadError));
            if (self->ownerThread_) {
                self->ownerThread_->start();
            }
        }
    };

    auto onComplete = [weak]() {
        LogI("data source complete.");
    };

    // 网络流（HttpStream/HLSStream）需要 owner 线程绑定：open() 内经 StreamFactory 构造函数注入
    mediaSourceManager_.setOwnerThread(ownerThread_);
    if (!mediaSourceManager_.open(path, std::move(onData), std::move(onError),
                                  std::move(onComplete))) {
        LogE("open media source failed, path:{}", path);
        setState(PlayerState::Error);
        // open 失败时没有 originErrorCode，host 从 URI 解析
        const Error err = isNetworkLink(path) ? Error::NetworkError : Error::IOError;
        notifyPlayerEvent(PlayerEvent::OnError,
                          formatErrorValue(err, 0, StringUtil::extractHostFromUri(path)));
        return false;
    }
    return true;
}

bool Player::Impl::createDemuxerComponent() noexcept {
    if (demuxerComponent_) {
        return true;
    }

    DemuxerConfig config;
    config.filePath = mediaSourceManager_.streamInfo().uri;
    if (config.filePath.empty()) {
        LogE("demuxer config file path is empty.");
        return false;
    }
    const auto filePath = config.filePath;

    auto [sender, receiver] = Channel<DataPacket>::create();
    dataPacketSender_ = std::move(sender);

    demuxerComponent_ = std::make_shared<DemuxerComponent>(std::move(receiver), std::move(config));
    demuxerComponent_->setHandleSeekFunc([weak = weak_from_this()](Range range) {
        auto self = weak.lock();
        if (!self || self->isStopped_) {
            return;
        }
        auto res = self->mediaSourceManager_.seek(range.start());
        if (!res.has_value()) {
            self->notifyPlayerEvent(PlayerEvent::OnError,
                                    std::to_string(static_cast<uint32_t>(res.error())));
            // 同 onError：投递到 ownerThread 执行，避免在 demuxer 线程并发销毁 pipeline_
            if (self->sender_) {
                self->sender_->send(buildEvent(EventType::ReadError));
                if (self->ownerThread_) {
                    self->ownerThread_->start();
                }
            }
        }
        LogI("demuxer seek to pos:{}, ok:{}", range.toString(), res.has_value());
    });
    demuxerComponent_->start();
    LogI("demuxer component created, uri:{}", filePath);
    return true;
}

void Player::Impl::buildPipeline() noexcept {
    pipeline_ = std::make_shared<PlayPipeline>(
        buildBackpressureConfig(mediaSourceManager_.streamType(), mediaSourceManager_.isLive()));
    pipeline_->setEventCallback(
        [weak = weak_from_this()](PipelineEvent event, std::optional<PipelineError> error) {
            auto self = weak.lock();
            if (!self || self->isStopped_) {
                return;
            }
            self->handlePipelineEvent(event, std::move(error));
        });
    pipeline_->setWakeupCallback([weak = weak_from_this()] {
        auto self = weak.lock();
        if (!self || !self->ownerThread_) {
            return;
        }
        self->ownerThread_->start();
    });
    pipeline_->setStreamControl(
        [weak = weak_from_this()] {
            auto self = weak.lock();
            if (!self) {
                return;
            }
            self->mediaSourceManager_.pause();
        },
        [weak = weak_from_this()] {
            auto self = weak.lock();
            if (!self) {
                return;
            }
            self->mediaSourceManager_.start();
        });
    // 注入 HLS 时间 seek 回调，PlayPipeline 检测到 HLS 时走此路径
    pipeline_->setSeekByTimeFunc([weak = weak_from_this()](double timeSec) -> StreamResult<void> {
        auto self = weak.lock();
        if (!self) {
            return std::unexpected(Error::InvalidOperation);
        }
        return self->mediaSourceManager_.seekByTime(timeSec);
    });
    // 传递 stream 类型，供 PlayPipeline::seek 区分 HLS/HTTP/File 路径
    pipeline_->setStreamType(mediaSourceManager_.streamType());
    pipeline_->setDemuxerComponent(demuxerComponent_);
    if (auto render = videoRender_.load()) {
        pipeline_->setVideoRender(render);
    }
}

void Player::Impl::createAudioComponent(const PlayerSetting& setting) noexcept {
    if (!pipeline_ || !demuxerComponent_ || !demuxerComponent_->hasAudio()) {
        return;
    }
    auto audioInfo = demuxerComponent_->audioInfo();
    if (!audioInfo) {
        return;
    }

    auto decodeType = DecoderManager::shareInstance().getDecoderType(audioInfo->mediaInfo,
                                                                     setting.enableAudioSoftDecode);
    if (!DecoderManager::shareInstance().contains(decodeType)) {
        LogE("not found decoder:media info {}", audioInfo->mediaInfo);
        return;
    }

    auto config = std::make_shared<AudioDecoderConfig>();
    config->playerId = playerId_;
    config->initWithAudioInfo(audioInfo);
    pipeline_->setupAudioTrack(decodeType, config, audioInfo, setting.volume / 100.0f);
    LogI("audio track setup, decoderType:{}", static_cast<uint32_t>(decodeType));
}

void Player::Impl::createVideoComponent(const PlayerSetting& setting) noexcept {
    if (!pipeline_ || !demuxerComponent_ || !demuxerComponent_->hasVideo()) {
        return;
    }
    auto videoInfo = demuxerComponent_->videoInfo();
    if (!videoInfo) {
        return;
    }

    auto decodeType = DecoderManager::shareInstance().getDecoderType(videoInfo->mediaInfo,
                                                                     setting.enableVideoSoftDecode);
    if (!DecoderManager::shareInstance().contains(decodeType)) {
        LogE("not found decoder:media info {}", videoInfo->mediaInfo);
        return;
    }

    if (auto render = videoRender_.load()) {
        render->notifyVideoInfo(videoInfo);
    }

    auto config = std::make_shared<VideoDecoderConfig>();
    config->playerId = playerId_;
    config->initWithVideoInfo(videoInfo);
    pipeline_->setVideoFps(static_cast<double>(videoInfo->fps));
    pipeline_->setupVideoTrack(decodeType, config);
    LogI("video track setup, decoderType:{}", static_cast<uint32_t>(decodeType));
}

void Player::Impl::preparePlayerInfo() noexcept {
    if (!demuxerComponent_) {
        LogE("demuxer component is not created.");
        return;
    }

    info_.isValid = true;
    info_.hasAudio = demuxerComponent_->hasAudio();
    info_.hasVideo = demuxerComponent_->hasVideo();
    info_.duration = demuxerComponent_->totalDuration().second();
    LogI("player info prepared, duration:{:.3f}s, hasAudio:{}, hasVideo:{}", info_.duration,
         info_.hasAudio, info_.hasVideo);

    setState(PlayerState::Prepared);

    PlayerSetting setting;
    params_.withReadLock([&setting](auto& p) {
        if (p) {
            setting = p->setting;
        }
    });

    if (info_.hasAudio) {
        createAudioComponent(setting);
    }
    if (info_.hasVideo) {
        createVideoComponent(setting);
    }

    setState(PlayerState::Buffering);
    pipeline_->build();
    ownerThread_->start();
    mediaSourceManager_.start();
}

void Player::Impl::process() noexcept {
    // 取本地强引用保活整个 process 周期，避免期间 pipeline_ 被 reset 导致 UAF
    auto pipeline = pipeline_;
    if (!pipeline) {
        return;
    }
    // 懒绑定：确保 PlayPipeline 的 ownerThreadId_ 指向 playerThread
    std::call_once(bindOwnerThreadFlag_, [&pipeline] {
        pipeline->bindToCurrentThread();
    });
    if (!pipeline->isBuilt()) {
        pipeline->build();
    }
    handleEvent(receiver_->tryReceiveAll());
    if (isStopped_) {
        return;
    }
    pipeline->drainPendingTasks();
    processPendingSeek();
}

void Player::Impl::doPlay() noexcept {
    LogI("do play");
    if (!info_.isValid || !pipeline_) {
        return;
    }
    ownerThread_->start();
    pipeline_->start();
    mediaSourceManager_.start();
}

void Player::Impl::doPause() noexcept {
    LogI("do pause");
    if (!info_.isValid || !pipeline_) {
        return;
    }
    pipeline_->pause();
    mediaSourceManager_.pause();
}

void Player::Impl::doStop() noexcept {
    LogI("do stop start");
    isStopped_.store(true, std::memory_order_release);
    {
        // 与 forceCleanup 互斥，防止双线程同时 teardown
        std::lock_guard<std::mutex> lock(releaseMutex_);
        if (isReleased_.load(std::memory_order_acquire)) {
            LogI("do stop: already released by forceCleanup, skip");
            return;
        }
        isReleased_ = true;
    }
    if (ownerThread_) {
        ownerThread_->cancelRepeatTask(fastTickTaskId_);
        ownerThread_->cancelRepeatTask(playedTimeTaskId_);
        ownerThread_->cancelRepeatTask(cacheTickTaskId_);
    }
    mediaSourceManager_.close();
    // 先关闭 Channel，触发 DemuxerComponent worker 收到 Closed 自然退出
    dataPacketSender_.reset();

    if (pipeline_) {
        pipeline_->teardown();
        pipeline_.reset();
    }
    demuxerComponent_.reset();
    notifyCenter_.resetNotifyState();
    stats_.reset();

    // ownerThread_ 的 stop/join 由 ~Impl() 在外部线程执行，此处不操作
    GLContextManager::shareInstance().removeMainContext(playerId_);
    cond_.notify_all();
    LogI("do stop end");
}

void Player::Impl::doSeek(PlayerSeekRequest seekRequest) noexcept {
    if (!pipeline_ || isStopped_ || isReleased_) {
        return;
    }

    auto seekTime = seekRequest.seekTime;
    auto playedTime = currentPlayedTime();
    if (isEqual(playedTime, seekTime, kSeekThreshold)) {
        LogI("discard seek:{} (too close to current pos {:.3f})", seekTime, playedTime);
        return;
    }

    // 非精准 seek in-flight merge：直接更新 target 到 pipeline，不切状态
    if (!seekRequest.isAccurate && seekRequest_.isValid()) {
        LogI("in-flight drag seek merge to {:.3f}s", seekTime);
        if (auto req = seekRequest_.load()) {
            req->seekTime = seekTime;
        }
        pipeline_->seek(CTime(seekTime), false);
        return;
    }

    auto nowState = state();
    setState(PlayerState::Pause);
    if (nowState == PlayerState::Playing) {
        stats_.resumeAfterBuffering = true;
    }

    seekRequest.startTime = Time::nowTimeStamp();
    seekRequest.seekEpoch = pipeline_->nextSeekEpoch();
    seekRequest_.reset(std::make_shared<PlayerSeekRequest>(seekRequest));
    LogI("do seek to:{:.3f}s, accurate:{}", seekTime, seekRequest.isAccurate);
    setState(PlayerState::Buffering);
    pipeline_->seek(CTime(seekTime), seekRequest.isAccurate);
}

void Player::Impl::doLoop() noexcept {
    if (!pipeline_) {
        return;
    }
    LogI("do loop");
    pipeline_->seek(CTime(0.0));
    mediaSourceManager_.start();
    pipeline_->start();
}

void Player::Impl::setState(PlayerState state) noexcept {
    PlayerState oldState = PlayerState::Unknown;
    bool isChanged = false;
    state_.withWriteLock([state, &isChanged, &oldState](auto& nowState) {
        if (nowState == state) {
            return;
        }
        if (!PlayerStateMachine::isValidTransition(nowState, state)) {
            LogW("invalid state transition: {} -> {}", static_cast<uint32_t>(nowState),
                 static_cast<uint32_t>(state));
            return;
        }
        oldState = nowState;
        nowState = state;
        isChanged = true;
    });
    if (!isChanged) {
        return;
    }

    LogI("setState: {} -> {}", static_cast<uint32_t>(oldState), static_cast<uint32_t>(state));
    if (state == PlayerState::Playing) {
        doPlay();
    } else if (state == PlayerState::Pause || state == PlayerState::Completed) {
        doPause();
    } else if (state == PlayerState::Stop || state == PlayerState::Error) {
        doStop();
    }
    notifyPlayerState(state);
}

void Player::Impl::handleSettingUpdate(Event& t) noexcept {
    if (t.type == EventType::UpdateSettingVolume) {
        auto volume = std::any_cast<float>(t.data);
        volume = std::max(0.0f, std::min(volume, 100.0f));
        if (pipeline_) {
            pipeline_->setVolume(volume / 100.0f);
        }
        params_.withWriteLock([volume](auto& p) {
            if (p) {
                p->setting.volume = volume;
            }
        });
    } else if (t.type == EventType::UpdateSettingMute) {
        auto isMute = std::any_cast<bool>(t.data);
        if (pipeline_) {
            if (isMute) {
                pipeline_->setVolume(0.0f);
            } else {
                float volume = 100.0f;
                params_.withReadLock([&volume](auto& p) {
                    if (p) {
                        volume = p->setting.volume;
                    }
                });
                pipeline_->setVolume(volume / 100.0f);
            }
        }
        params_.withWriteLock([isMute](auto& p) {
            if (p) {
                p->setting.isMute = isMute;
            }
        });
    }
}

void Player::Impl::handleEvent(std::deque<EventPtr>&& events) noexcept {
    PlayerState changeState = PlayerState::Unknown;
    auto currentState = state();
    bool hasPendingSeek = pendingSeekTarget_.load(std::memory_order_acquire) >= 0.0;
    for (auto& event : events) {
        if (!event) {
            continue;
        }
        if (event->type == EventType::Seek) {
            auto seekRequest = std::any_cast<PlayerSeekRequest>(event->data);
            doSeek(seekRequest);
        } else if (EventType::UpdateSetting < event->type &&
                   event->type < EventType::UpdateSettingEnd) {
            handleSettingUpdate(*event);
        } else if (auto state = PlayerStateMachine::getStateFromEvent(event->type);
                   state.has_value()) {
            if (currentState == PlayerState::Stop || currentState == PlayerState::Error) {
                continue;
            }
            if (currentState == PlayerState::Completed && *state == PlayerState::Playing) {
                LogI("replay from completed, seek to 0");
                doSeek(PlayerSeekRequest{.isAccurate = false, .seekTime = 0.0});
            }
            if (hasPendingSeek && *state == PlayerState::Playing) {
                stats_.resumeAfterBuffering = true;
                continue;
            }
            changeState = state.value();
        }
    }

    if (changeState != PlayerState::Unknown) {
        setState(changeState);
    }
}

void Player::Impl::processPendingSeek() noexcept {
    auto target = pendingSeekTarget_.load(std::memory_order_acquire);
    if (target < 0.0) {
        return;
    }
    if (!pipeline_ || isStopped_ || isReleased_) {
        return;
    }

    // 精准 seek 进行中时保留 pending，等完成后消费
    if (seekRequest_.isValid()) {
        if (auto req = seekRequest_.load(); req && req->isAccurate) {
            return;
        }
        // 非精准 seek 进行中：继续往下走，doSeek 中会走 in-flight merge
    }

    // 同一 tick 内精准 seek 刚完成时跳过，等下一 tick 再处理拖拽 seek
    bool expected = true;
    if (seekJustCompleted_.compare_exchange_strong(expected, false, std::memory_order_acq_rel)) {
        return;
    }

    pendingSeekTarget_.store(-1.0, std::memory_order_release);
    doSeek(PlayerSeekRequest{.isAccurate = false, .seekTime = target});
}

void Player::Impl::resumeFromBuffering() noexcept {
    if (state() != PlayerState::Buffering) {
        return;
    }
    if (stats_.resumeAfterBuffering) {
        stats_.resumeAfterBuffering = false;
        LogI("resume from buffering -> playing");
        setState(PlayerState::Playing);
    } else {
        LogI("resume from buffering -> pause");
        setState(PlayerState::Pause);
    }
}

void Player::Impl::handlePipelineEvent(PipelineEvent event,
                                       std::optional<PipelineError> error) noexcept {
    switch (event) {
        case PipelineEvent::DemuxHeaderParsed:
            preparePlayerInfo();
            break;
        case PipelineEvent::PipelineReady:
            break;
        case PipelineEvent::FirstFrameRendered:
            notifyPlayerEvent(PlayerEvent::FirstFrameRendered);
            resumeFromBuffering();
            break;
        case PipelineEvent::BufferingEnd:
            LogI("buffering end");
            resumeFromBuffering();
            break;
        case PipelineEvent::BufferingStart:
            if (state() == PlayerState::Playing) {
                LogI("buffering start");
                stats_.resumeAfterBuffering = true;
                setState(PlayerState::Buffering);
            }
            break;
        case PipelineEvent::SeekCompleted:
            if (auto seekRequest = seekRequest_.load()) {
                if (seekRequest->seekEpoch != pipeline_->currentSeekEpoch()) {
                    break;
                }
                LogI("seek done, cost time:{}",
                     (Time::nowTimeStamp() - seekRequest->startTime).toMilliSeconds());
                seekRequest_.reset();
                if (seekRequest->isAccurate) {
                    seekJustCompleted_.store(true, std::memory_order_release);
                }
                notifyPlayerEvent(PlayerEvent::SeekDone);
            }
            resumeFromBuffering();
            break;
        case PipelineEvent::AudioDecoderFailed:
        case PipelineEvent::VideoDecoderFailed: {
            const auto err = error.has_value() ? error->error : Error::PipelineDecodeError;
            LogE("decoder init failed, error:{}", static_cast<uint32_t>(err));
            notifyPlayerEvent(PlayerEvent::OnError, std::to_string(static_cast<uint32_t>(err)));
            setState(PlayerState::Error);
            break;
        }
        case PipelineEvent::ReadError:
        case PipelineEvent::DecodeError: {
            // 如果已经在停止或出错状态，忽略此类错误（可能是 stop 过程中触发的）
            auto currentState = state();
            if (currentState == PlayerState::Stop || currentState == PlayerState::Error) {
                LogW("pipeline error ignored, event:{}, error:{}, current state:{}",
                     static_cast<uint32_t>(event),
                     error.has_value() ? static_cast<uint32_t>(error->error) : 0,
                     static_cast<uint32_t>(currentState));
                break;
            }
            const auto err = error.has_value() ? error->error : Error::PipelineDecodeError;
            LogE("pipeline error, event:{}, error:{}", static_cast<uint32_t>(event),
                 static_cast<uint32_t>(err));
            notifyPlayerEvent(PlayerEvent::OnError, std::to_string(static_cast<uint32_t>(err)));
            setState(PlayerState::Error);
            break;
        }
        case PipelineEvent::RenderEnd: {
            LogI("render end, playback completed");
            notifyPlayedTime(true);
            notifyPlayerEvent(PlayerEvent::PlayEnd);
            bool isLoop = false;
            params_.withReadLock([&isLoop](auto& p) {
                if (p) {
                    isLoop = p->setting.isLoop;
                }
            });
            if (isLoop) {
                doLoop();
            } else {
                setState(PlayerState::Completed);
                if (ownerThread_) {
                    ownerThread_->pause();
                }
            }
            break;
        }
    }
}

void Player::Impl::notifyPlayerEvent(PlayerEvent event, std::string value) noexcept {
    notifyCenter_.notifyPlayerEvent(event, std::move(value));
}

std::string Player::Impl::formatErrorValue(Error err,
                                           int32_t originErrorCode,
                                           std::string_view host) noexcept {
    // value = "slarkError|originErrorCode|host"，上层按 `|` 切分
    return std::to_string(static_cast<uint32_t>(err)) + "|" + std::to_string(originErrorCode) +
           "|" + std::string(host);
}

void Player::Impl::notifyPlayerState(PlayerState state) noexcept {
    notifyCenter_.notifyPlayerState(state);
}

void Player::Impl::notifyPlayedTime(bool isEndTime) noexcept {
    double audioTime = pipeline_ ? pipeline_->audioTime().second() : 0.0;
    double videoTime = pipeline_ ? pipeline_->videoTime().second() : 0.0;
    notifyCenter_.notifyPlayedTime(info_.duration, audioTime, videoTime, info_.hasAudio,
                                   info_.hasVideo, isEndTime);
}

void Player::Impl::notifyCacheTime() noexcept {
    if (!pipeline_) {
        return;
    }
    notifyCenter_.notifyCacheTime(pipeline_->cacheEndTime());
}

PlayerParams Player::Impl::params() noexcept {
    PlayerParams params;
    params_.withReadLock([&params](auto& p) {
        if (p) {
            params = *p;
        }
    });
    return params;
}

void Player::Impl::setLoop(bool isLoop) noexcept {
    params_.withWriteLock([isLoop](auto& p) {
        if (p) {
            p->setting.isLoop = isLoop;
        }
    });
    if (ownerThread_) {
        ownerThread_->start();
    }
}

void Player::Impl::setVolume(float volume) noexcept {
    if (!sender_) {
        LogE("error!! not init");
        return;
    }
    auto ptr = buildEvent(EventType::UpdateSettingVolume);
    ptr->data = std::make_any<float>(volume);
    sender_->post(std::move(ptr));
    ownerThread_->start();
}

void Player::Impl::setMute(bool isMute) noexcept {
    if (!sender_) {
        LogE("error!! not init");
        return;
    }
    auto ptr = buildEvent(EventType::UpdateSettingMute);
    ptr->data = std::make_any<bool>(isMute);
    sender_->post(std::move(ptr));
    ownerThread_->start();
}

PlayerState Player::Impl::state() const noexcept {
    PlayerState state = PlayerState::NotInited;
    state_.withReadLock([&state](auto& nowState) {
        state = nowState;
    });
    return state;
}

double Player::Impl::currentPlayedTime() const noexcept {
    if (!pipeline_) {
        return 0.0;
    }
    return pipeline_->currentTime().second();
}

double Player::Impl::demuxedDuration() const noexcept {
    if (!pipeline_) {
        return 0.0;
    }
    return pipeline_->cachedDuration();
}

AVFrameRefPtr Player::Impl::requestRender() noexcept {
    if (!pipeline_) {
        return nullptr;
    }
    return pipeline_->requestVideoFrame();
}

void Player::Impl::setRenderImpl(std::weak_ptr<IVideoRender> render) noexcept {
    videoRender_.store(std::move(render));
    auto ptr = videoRender_.load();
    if (!ptr) {
        return;
    }
    ptr->setPlayerId(playerId_);
    auto weak = weak_from_this();
    ptr->setRequestRenderFunc([weak = std::move(weak)]() -> AVFrameRefPtr {
        auto self = weak.lock();
        if (!self || self->isStopped_) {
            return nullptr;
        }
        return self->requestRender();
    });
    if (pipeline_) {
        pipeline_->setVideoRender(ptr);
    }
    if (demuxerComponent_ && demuxerComponent_->hasVideo()) {
        ptr->notifyVideoInfo(demuxerComponent_->videoInfo());
    }
}

}  // namespace slark