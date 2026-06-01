//
//  PlayerImpl.cpp
//  slark
//
//  Created by Nevermore on 2022/5/4.
//

#include "PlayerImpl.h"

#include "Base.h"
#include "DecoderConfig.h"
#include "GLContextManager.h"
#include "Log.hpp"
#include "MediaUtil.h"
#include "PlayerStateMachine.h"
#include "Random.hpp"
#include "VideoInfo.h"

#include <algorithm>
#include <chrono>
#include <optional>
#include <utility>

namespace slark {

namespace {
constexpr double kSeekThreshold = 0.05; // 50ms 内跳过 seek

BackpressureConfig buildBackpressureConfig(StreamType type, bool isLive) {
    if (isLive || type == StreamType::HLS) {
        return BackpressureConfig::forLive();
    }
    if (type == StreamType::Http) {
        return BackpressureConfig::forNetwork();
    }
    return BackpressureConfig::forLocal();
}
} // namespace

Player::Impl::Impl(std::unique_ptr<PlayerParams> params)
    : playerId_(Random::uuid()), notifyCenter_(playerId_) {
    SAssert(params != nullptr, "PlayerParams must not be null");
    GLContextManager::shareInstance().addMainContext(playerId_, params->mainGLContext);
    params_.withWriteLock([&params](auto& p) {
        p = std::move(params);
    });
}

Player::Impl::~Impl() {
    isStopped_ = true;
    if (ownerThread_) {
        updateState(PlayerState::Stop);
        std::unique_lock<std::mutex> lock(releaseMutex_);
        // 超时保护：防止 ownerThread_ 卡死导致析构永久阻塞
        if (!cond_.wait_for(lock, std::chrono::seconds(5), [this] {
                return isReleased_.load();
            })) {
            LogE("PlayerImpl destructor: wait for release timeout");
        }
        // doStop 内部不 join ownerThread_，由此处在外部线程安全地 stop + join
        ownerThread_->stop();
        ownerThread_.reset();
    }
}

void Player::Impl::seek(double time, bool isAccurate) noexcept {
    if (!sender_) {
        LogE("error!! not init");
        return;
    }
    auto ptr = buildEvent(EventType::Seek);
    PlayerSeekRequest req;
    req.seekTime = time;
    req.isAccurate = isAccurate;
    ptr->data = req;
    sender_->send(std::move(ptr));
    ownerThread_->start();
    LogI("receive seek:{}, isAccurate:{}", time, isAccurate);
}

void Player::Impl::updateState(PlayerState state) noexcept {
    if (!sender_) {
        LogE("error!! not init");
        return;
    }
    sender_->send(buildEvent(state));
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

    ownerThread_ = Thread::create("playerThread");
    ownerThread_->postRepeatTask(std::chrono::milliseconds(setting.playerThreadTickMs), [this]() {
        process();
        if (pipeline_ && pipeline_->isBuilt()) {
            pipeline_->onFastTick();
        }
    });
    ownerThread_->postRepeatTask(std::chrono::milliseconds(setting.playedTimeTickMs), [this]() {
        if (state() == PlayerState::Playing) {
            notifyPlayedTime();
        }
    });
    ownerThread_->postRepeatTask(std::chrono::milliseconds(setting.cacheTickMs), [this]() {
        if (isStopped_) {
            return;
        }
        notifyCacheTime();
        if (pipeline_ && pipeline_->isBuilt()) {
            pipeline_->onHeartbeat();
        }
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
        (void)self->dataPacketSender_->send(std::move(data));
    };

    auto onError = [weak](bool isNetwork) {
        auto self = weak.lock();
        if (!self || self->isStopped_) {
            return;
        }
        const auto err = isNetwork ? Error::NetworkError : Error::IOError;
        self->notifyPlayerEvent(PlayerEvent::OnError, std::to_string(static_cast<uint32_t>(err)));
        self->setState(PlayerState::Error);
    };

    auto onComplete = [weak]() {
        LogI("data source complete.");
    };

    if (!mediaSourceManager_.open(path,
                                  std::move(onData),
                                  std::move(onError),
                                  std::move(onComplete))) {
        LogE("open media source failed, path:{}", path);
        setState(PlayerState::Error);
        const bool isNet = isNetworkLink(path);
        auto errorCode = isNet ? std::to_string(static_cast<uint32_t>(Error::NetworkError))
                               : std::to_string(static_cast<uint32_t>(Error::IOError));
        notifyPlayerEvent(PlayerEvent::OnError, errorCode);
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

    config.fileSize = mediaSourceManager_.streamInfo().totalSize;

    // 创建 Channel，持有 sender，将 receiver 传给 DemuxerComponent
    auto [sender, receiver] = Channel<DataPacket>::create();
    dataPacketSender_ = std::move(sender);

    demuxerComponent_ = std::make_shared<DemuxerComponent>(std::move(receiver), std::move(config));
    demuxerComponent_->setHandleSeekFunc([weak = weak_from_this()](Range range) {
        auto self = weak.lock();
        if (!self || self->isStopped_) {
            return;
        }
        self->demuxerComponent_->flush();
        // 取消当前 HTTP 请求并重新发起指定 Range 的请求，确保 moov box 在文件末尾时能正确重定位
        auto res = self->mediaSourceManager_.seek(range.start());
        if (!res.has_value()) {
            // 数据源拒绝 seek（如不支持 Range），立即上报错误，避免 UI 卡在 Buffering
            self->notifyPlayerEvent(PlayerEvent::OnError,
                                    std::to_string(static_cast<uint32_t>(res.error())));
            self->setState(PlayerState::Error);
        }
        LogI("demuxer seek to pos:{}, ok:{}", range.toString(), res.has_value());
    });
    demuxerComponent_->start();
    LogI("demuxer component created, uri:{}", config.filePath);
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
    LogI("player info prepared, duration:{:.3f}s, hasAudio:{}, hasVideo:{}",
         info_.duration,
         info_.hasAudio,
         info_.hasVideo);

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
    doPlay(); // 首帧快速推帧：pipeline start，不做 AV Sync，不做背压 Hold
}

void Player::Impl::process() noexcept {
    if (!pipeline_) {
        return;
    }
    if (!pipeline_->isBuilt()) {
        pipeline_->build();
    }
    handleEvent(receiver_->tryReceiveAll());
    if (isStopped_) {
        return;
    }
    pipeline_->drainPendingTasks();
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
    {
        // 保证 isReleased_ 的写入与 cond_.wait 的 happens-before 关系
        std::lock_guard<std::mutex> lock(releaseMutex_);
        isReleased_ = true;
    }
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
    if (!seekRequest.isAccurate && seekRequest_.isValid()) {
        LogI("discard non-accurate seek:{} (seek in progress)", seekTime);
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
    bool isChanged = false;
    state_.withWriteLock([state, &isChanged](auto& nowState) {
        if (nowState == state) {
            return;
        }
        if (!PlayerStateMachine::isValidTransition(nowState, state)) {
            LogW("invalid state transition: {} -> {}",
                 static_cast<uint32_t>(nowState),
                 static_cast<uint32_t>(state));
            return;
        }
        nowState = state;
        isChanged = true;
    });
    if (!isChanged) {
        return;
    }

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

void Player::Impl::handleEvent(std::list<EventPtr>&& events) noexcept {
    PlayerState changeState = PlayerState::Unknown;
    auto currentState = state();
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
            changeState = state.value();
        }
    }

    if (changeState != PlayerState::Unknown) {
        setState(changeState);
    }
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
            // 直接在 ownerThread 上准备，无需绕行 sender_
            preparePlayerInfo();
            break;
        case PipelineEvent::PipelineReady:
            // 解码器就绪，无需额外处理，首帧渲染后才进入 Ready
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
                notifyPlayerEvent(PlayerEvent::SeekDone);
            }
            resumeFromBuffering();
            break;
        case PipelineEvent::SeekTimeout:
            // seek 被覆盖，恢复到 seek 前的状态
            LogW("seek timeout, discard seek request");
            seekRequest_.reset();
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
            const auto err = error.has_value() ? error->error : Error::PipelineDecodeError;
            LogE("pipeline error, event:{}, error:{}",
                 static_cast<uint32_t>(event),
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

void Player::Impl::notifyPlayerState(PlayerState state) noexcept {
    notifyCenter_.notifyPlayerState(state);
}

void Player::Impl::notifyPlayedTime(bool isEndTime) noexcept {
    double audioTime = pipeline_ ? pipeline_->audioTime().second() : 0.0;
    double videoTime = pipeline_ ? pipeline_->currentTime().second() : 0.0;
    notifyCenter_.notifyPlayedTime(info_.duration,
                                   audioTime,
                                   videoTime,
                                   info_.hasAudio,
                                   info_.hasVideo,
                                   isEndTime);
}

void Player::Impl::notifyCacheTime() noexcept {
    notifyCenter_.notifyCacheTime(demuxedDuration());
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
    sender_->send(std::move(ptr));
    ownerThread_->start();
}

void Player::Impl::setMute(bool isMute) noexcept {
    if (!sender_) {
        LogE("error!! not init");
        return;
    }
    auto ptr = buildEvent(EventType::UpdateSettingMute);
    ptr->data = std::make_any<bool>(isMute);
    sender_->send(std::move(ptr));
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

} // namespace slark
