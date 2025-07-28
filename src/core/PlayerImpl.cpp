//
//  PlayerImpl.cpp
//  slark
//
//  Created by Nevermore on 2022/5/4.
//

#include "Player.h"
#include "Log.hpp"
#include "PlayerImpl.h"
#include "PlayerImplHelper.h"
#include "Util.hpp"
#include "Base.h"
#include "AudioRenderComponent.h"
#include "DecoderComponent.h"
#include "Event.h"
#include "IDemuxer.h"
#include "DecoderConfig.h"
#include "GLContextManager.h"
#include "MediaUtil.h"
#include "Clock.h"
#include "HLSReader.h"

namespace slark {

const double kAVSyncMinThreshold = 0.1; //100ms
const double kAVSyncMaxThreshold = 10; //10s
const double kMinCanPlayTime = 0.5; //500ms
const uint32_t kMaxCacheDecodedVideoFrame = 8;
const uint32_t kMaxCacheDecodedAudioFrame = 10;
constexpr double kMinPushDecodeTime = 0.2; //second

Player::Impl::Impl(std::unique_ptr<PlayerParams> params)
    : playerId_(Random::uuid()) {
    GLContextManager::shareInstance().addMainContext(playerId_, params->mainGLContext);
    params_.withWriteLock([&params](auto& p){
        p = std::move(params);
    });
}

Player::Impl::~Impl() {
    isStopped_ = true;
    if (ownerThread_) {
        updateState(PlayerState::Stop);
        std::unique_lock<std::mutex> lock(releaseMutex_);
        cond_.wait(lock, [this]{
            return isReleased_.load();
        });
    }
}

void Player::Impl::seek(
    double time,
    bool isAccurate
) noexcept {
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

void Player::Impl::updateState(
    PlayerState state
) noexcept {
    if (!sender_) {
        LogE("error!! not init");
        return;
    }
    sender_->send(buildEvent(state));
    ownerThread_->start();
}

void Player::Impl::addObserver(
    IPlayerObserverPtr observer
) noexcept {
    observer_.withWriteLock([observer = std::move(observer)] (auto& ob) {
        ob = observer;
    });
}

void Player::Impl::removeObserver() noexcept {
    observer_.withWriteLock([](auto& observer) {
        observer.reset();
    });
}

void Player::Impl::init() noexcept {
    helper_ = std::make_unique<PlayerImplHelper>(weak_from_this());
    helper_->debugInfo.receiveTime = Time::nowTimeStamp();
    using namespace std::chrono_literals;
    setState(PlayerState::Initializing);
    auto [sp, rp] = Channel<EventPtr>::create();
    sender_ = std::move(sp);
    receiver_ = std::move(rp);

    if (!setupDataProvider()) {
        LogE("setupIOHandler failed.");
        return;
    }
    ownerThread_ = std::make_unique<Thread>("playerThread", &Player::Impl::process, this);
    ownerThread_->setInterval(10ms);
    ownerThread_->runLoop(200ms, [this](){
        if (state() == PlayerState::Playing) {
            notifyPlayedTime();
        }
    });
    ownerThread_->runLoop(500ms, [this]() {
        if (!isStopped_) {
            notifyCacheTime();
            checkCacheState();
        }
    });
    
    dataProvider_->start();
    ownerThread_->start();
    LogI("player Initializing start.");
    helper_->debugInfo.createdTime = Time::nowTimeStamp();
}

bool Player::Impl::setupDataProvider() noexcept {
    ///set file path
    std::string path;
    params_.withReadLock([&](auto& params){
        path = params->item.path;
    });
    
    if (path.empty()) {
        LogE("error, player param invalid.");
        setState(PlayerState::Stop);
        return false;
    }

    auto weak = weak_from_this();
    auto readDataCallback = [weak = std::move(weak)]
        (IReader* dataProvider, DataPacket data, IOState state) {
        auto self = weak.lock();
        if (!self || self->isStopped_) {
            LogE("PlayerImpl is released, cannot process data.");
            return;
        }
        LogI("receive data offset:{} size: {}", data.offset, data.length());
        if (data.data) {
            self->dataList_.withLock([&](auto& dataList) {
                dataList.emplace_back(std::move(data));
            });
        }
        
        if (state == IOState::Error) {
            self->sender_->send(buildEvent(EventType::ReadError));
            auto errorCode = dataProvider->isNetwork() ?
                std::to_string(static_cast<uint8_t>(PlayerErrorCode::NetWorkError)) :
                std::to_string(static_cast<uint8_t>(PlayerErrorCode::OpenFileError));
            self->notifyPlayerEvent(PlayerEvent::OnError, errorCode);
        }
    };

    if (!helper_->createDataProvider(path, std::move(readDataCallback))) {
        setState(PlayerState::Error);
        auto errorCode = isNetworkLink(path) ?
                         std::to_string(static_cast<uint8_t>(PlayerErrorCode::NetWorkError)) :
                         std::to_string(static_cast<uint8_t>(PlayerErrorCode::OpenFileError));
        notifyPlayerEvent(PlayerEvent::OnError, errorCode);
        return false;
    }
    return true;
}

void Player::Impl::createAudioComponent(
    const PlayerSetting& setting
) noexcept {
    auto audioInfo = demuxerComponent_->audioInfo();
    auto decodeType = DecoderManager::shareInstance().getDecoderType(audioInfo->mediaInfo, setting.enableAudioSoftDecode);
    if (!DecoderManager::shareInstance().contains(decodeType)) {
        LogE("not found decoder:media info {}", audioInfo->mediaInfo);
        return;
    }
    audioDecodeComponent_ = std::make_shared<DecoderComponent>([this](auto frame) {
        if (isStopped_) {
            return;
        }

        if (auto seekRequest = seekRequest_.load();
            seekRequest && (frame->ptsTime() < seekRequest-> seekTime) ) {
            LogI("discard decoded audio frame pts:{}", frame->ptsTime());
            return;
        }
        LogI("decoded audio frame info:{}", frame->ptsTime());
        audioFrames_.withLock([&frame](auto& frames){
            frames.emplace_back(std::move(frame));
        });
    });
    helper_->debugInfo.createAudioDecoderTime = Time::nowTimeStamp();
    if (!audioDecodeComponent_) {
        LogI("create audio pushFrameDecode Component error:{}", audioInfo->mediaInfo);
        return;
    }
    LogI("open create audio decoder");
    auto config = std::make_shared<AudioDecoderConfig>();
    config->playerId = playerId_;
    config->initWithAudioInfo(audioInfo);
    if (!audioDecodeComponent_->open(decodeType, config)) {
        LogI("create audio pushFrameDecode component success");
        return ;
    }
    helper_->debugInfo.openedAudioDecoderTime = Time::nowTimeStamp();
    auto renderAudioInfo = audioInfo->copy();
    renderAudioInfo->bitsPerSample = 16; //default 16bit pcm
    audioRender_ = std::make_unique<AudioRenderComponent>(renderAudioInfo);
    audioRender_->setPullDataFunc([this](uint32_t requestSize) {
        AVFrameRefPtr frame;
        audioFrames_.withLock([&frame, requestSize](auto& audioFrames){
            if (audioFrames.empty()) {
                return;
            }
            if (audioFrames.front()->data->length < requestSize) {
                frame = std::move(audioFrames.front());
                audioFrames.pop_front();
            }
        });
        if (frame) {
            LogI("push audio:{}", frame->ptsTime());
        }
        return frame;
    });
    helper_->debugInfo.createAudioRenderTime = Time::nowTimeStamp();
    LogI("create audio render success");
}

void Player::Impl::createVideoComponent(
    const PlayerSetting& setting
) noexcept {
    if (!demuxerComponent_ || !demuxerComponent_->hasVideo()) {
        LogE("no video, cannot create video component.");
        return;
    }
    auto videoInfo = demuxerComponent_->videoInfo();
    auto decodeType = DecoderManager::shareInstance().getDecoderType(videoInfo->mediaInfo, setting.enableVideoSoftDecode);
    if (!DecoderManager::shareInstance().contains(decodeType)) {
        LogE("not found decoder:media info {}", videoInfo->mediaInfo);
        return;
    }
    videoDecodeComponent_ = std::make_shared<DecoderComponent>([this](auto frame) {
        if (isStopped_) {
            return;
        }
        if (auto seekRequest = seekRequest_.load();
            seekRequest && (frame->ptsTime() < seekRequest-> seekTime) ) {
            LogI("discard decoded video frame pts:{}", frame->ptsTime());
            return;
        }
        LogI("decoded video frame info:{}", frame->ptsTime());
        videoFrames_.withLock([&frame](auto& frames){
            frames.emplace(frame->pts, std::move(frame));
        });
    });
    helper_->debugInfo.createVideoDecoderTime = Time::nowTimeStamp();
    if (!videoDecodeComponent_) {
        LogI("create video decoder error:{}", videoInfo->mediaInfo);
        return;
    }
    auto config = std::make_shared<VideoDecoderConfig>();
    config->playerId = playerId_;
    config->initWithVideoInfo(videoInfo);
    if (videoDecodeComponent_->open(decodeType, config)) {
        LogI("create video decode component success");
    } else {
        return;
    }
    helper_->debugInfo.openedVideoDecoderTime = Time::nowTimeStamp();
    if (auto render = videoRender_.load()) {
        render->notifyVideoInfo(videoInfo);
    }
}

void Player::Impl::preparePlayerInfo() noexcept {
    //prepare player info, no lock is added here because it is only changed once
    if (!demuxerComponent_) {
        LogE("demuxer component is not created.");
        return;
    }
    {
        info_.isValid = true;
        info_.hasAudio = demuxerComponent_->hasAudio();
        info_.hasVideo = demuxerComponent_->hasVideo();
        info_.duration = demuxerComponent_->totalDuration().second();
    }
    setState(PlayerState::Prepared);
    PlayerSetting setting;
    params_.withReadLock([&setting](auto& p){
        setting = p->setting;
    });
    if (info_.hasAudio) {
        LogI("has audio, audio info:{}", demuxerComponent_->audioInfo()->mediaInfo);
        createAudioComponent(setting);
    } else {
        LogI("no audio");
    }
    
    if (info_.hasVideo) {
        LogI("has video, video info:{}", demuxerComponent_->videoInfo()->mediaInfo);
        createVideoComponent(setting);
    } else {
        LogI("no video");
    }
    doPause();
    if (info_.hasVideo) {
        dataProvider_->start(); //render first frame
        ownerThread_->start();
        stats_.setFastPush();
    }
    LogI("init player success.");
    setState(PlayerState::Buffering);
}

void Player::Impl::handleAudioPacket(
    AVFramePtrArray& audioPackets
) noexcept {
    if (!demuxerComponent_ || audioPackets.empty()) {
        return;
    }
    auto isDemuxCompleted = demuxerComponent_->isCompleted();
    std::optional<double> discardTime = std::nullopt;
    if (auto seekRequest = seekRequest_.load()) {
        discardTime = seekRequest->seekTime;
        LogI("receive seek request, discard time:{}", discardTime.value());
    }

    for (auto& packet : audioPackets) {
        auto pts = packet->ptsTime();
        stats_.audioDemuxedTime = pts + packet->duration / 1000.0;
        if (isDemuxCompleted) {
            stats_.audioDemuxedTime = info_.duration;
        }
        if (discardTime.has_value() && !isEqualOrGreater(pts, discardTime.value()) ) {
            LogI("discard audio frame:{}", packet->ptsTime());
            continue;
        }
        LogI("demux audio frame:{}, pts:{}, dts:{}", packet->index, packet->dtsTime(), packet->ptsTime());
        audioPackets_.withLock([&packet](auto& audioPackets) {
            audioPackets.emplace_back(std::move(packet));
        });
    }
}

void Player::Impl::handleVideoPacket(
    AVFramePtrArray& videoPackets
) noexcept {
    if (!demuxerComponent_ || videoPackets.empty()) {
        return;
    }
    auto isDemuxCompleted = demuxerComponent_->isCompleted();
    std::optional<double> discardTime = std::nullopt;
    if (auto seekRequest = seekRequest_.load()) {
        discardTime = seekRequest->seekTime;
        LogI("receive seek request, discard time:{}", discardTime.value());
    }
    if (isDemuxCompleted &&
        discardTime.has_value() &&
        discardTime.value() > videoPackets.back()->ptsTime()) {
        //Ensure that the last frame is not discarded
        discardTime = videoPackets.back()->ptsTime();
    }
    for (auto& packet : videoPackets) {
        auto pts = packet->ptsTime();
        if (isDemuxCompleted) {
            stats_.videoDemuxedTime = info_.duration;
        } else {
            stats_.videoDemuxedTime = pts + packet->duration / 1000.0;
        }
        if (discardTime.has_value() &&
            !isEqualOrGreater(pts, discardTime.value())) {
            packet->isDiscard = true;
            LogI("[seek info]discard video frame:{}, seek time:{}", pts, discardTime.value());
        }
        LogI("demux video frame:{}, pts:{}, dts:{}, isKey:{}, offset:{}, size:{}", packet->index, pts, packet->dtsTime(), packet->info->isKeyFrame(), packet->offset, packet->data->length);
        videoPackets_.withLock([&packet](auto& videoPackets) {
            videoPackets.emplace_back(std::move(packet));
        });
    }
}

bool Player::Impl::createDemuxerComponent() noexcept {
    DemuxerConfig config;
    params_.withReadLock([&config](auto& params) {
        if (!params) {
            LogE("player params is null.");
            return;
        }
        config.filePath = params->item.path;
    });
    if (config.filePath.empty()) {
        LogE("demuxer config file path is empty.");
        return false;
    }
    if (dataProvider_) {
        config.fileSize = dataProvider_->size();
    }
    demuxerComponent_ = std::make_shared<DemuxerComponent>(std::move(config));
    PlayerSetting setting;
    params_.withReadLock([&setting](auto& p){
        setting = p->setting;
    });
    demuxerComponent_->setHandleResultFunc([weak = weak_from_this(), maxCache = setting.maxCacheTime]
       (const std::shared_ptr<IDemuxer>& demuxer, DemuxerResult&& result) {
        auto self = weak.lock();
        if (!self || self->isStopped_) {
            return;
        }
        if (result.resultCode == DemuxerResultCode::ParsedHeader) {
            self->sender_->send(buildEvent(EventType::Prepared));
        } else if (result.resultCode == DemuxerResultCode::ParsedFPS) {
            if (auto render = self->videoRender_.load()) {
                render->notifyVideoInfo(demuxer->videoInfo());
            }
        }
        self->handleAudioPacket(result.audioFrames);
        self->handleVideoPacket(result.videoFrames);
        auto cachedDuration = self->demuxedDuration();
        auto playedTime = self->currentPlayedTime();
        auto cacheTime = cachedDuration - playedTime;
        cacheTime = std::max(0.0, cacheTime);
        if (cacheTime < maxCache) {
            return;
        }
        if (self->dataProvider_) {
            self->dataProvider_->pause();
        }
        if (self->demuxerComponent_) {
            self->demuxerComponent_->pause();
        }
        LogI("demuxer pause, cache time:{}", cacheTime);
    });
    demuxerComponent_->setHandleSeekFunc([weak = weak_from_this()]
       (Range range) {
        auto self = weak.lock();
        if (!self || self->isStopped_) {
            return;
        }
        self->dataList_.withLock([](auto& list) {
            list.clear(); //clear data list when seek
        });
        LogI("demuxer seek to pos:{}", range.toString());
        self->dataProvider_->updateReadRange(range);
    });
    demuxerComponent_->start();
    return true;
}

void Player::Impl::demuxData() noexcept {
    if (isStopped_) {
        return;
    }
    if (demuxerComponent_ == nullptr && !createDemuxerComponent()) {
        LogE("demuxer component is not created.");
        return;
    }
    std::list<DataPacket> dataList;
    dataList_.withLock([&](auto& list) {
        dataList.swap(list);
    });
    if (dataList.empty()) {
        return;
    }
    LogI("push demux data size:{}", dataList.size());
    demuxerComponent_->pushData(std::move(dataList));
}

void Player::Impl::pushAudioPacketDecode() noexcept {
    if (!audioDecodeComponent_ || !demuxerComponent_) {
        return;
    }
    auto audioTime = audioRenderTime();
    if (!isAudioNeedDecode(audioTime)) {
        if (!audioDecodeComponent_->isRunning()) {
            audioDecodeComponent_->start();
        }
        return;
    }
    audioPackets_.withLock([&audioTime, this](auto& audioPackets) {
        if (audioPackets.empty()) {
            if (demuxerComponent_->isCompleted() &&
                !audioDecodeComponent_->isDecodeCompleted()) {
                audioDecodeComponent_->setInputCompleted();
                LogI("input audio completed");
            }
            return;
        }
        auto& packet = audioPackets.front();
        LogI("[decode] push audio frame pushFrameDecode, dts:{}, audio time:{}", packet->dtsTime(), audioTime);
        audioDecodeComponent_->send(std::move(packet));
        audioPackets.pop_front();
    });
}

void Player::Impl::pushVideoPacketDecode() noexcept {
    constexpr int32_t kMaxPushCount = 3; //Push up to 5 frames at a time
    if (!videoDecodeComponent_ || !demuxerComponent_) {
        return;
    }
    auto videoTime = videoRenderTime();
    if (!isVideoNeedDecode(videoTime)) {
        if (!videoDecodeComponent_->isRunning()) {
            videoDecodeComponent_->start();
        }
        return;
    }
    videoPackets_.withLock([&videoTime, this](auto& videoPackets){
        if (videoPackets.empty()) {
            if (demuxerComponent_->isCompleted() &&
                !videoDecodeComponent_->isInputCompleted()) {
                videoDecodeComponent_->setInputCompleted();
                LogI("input video completed");
            }
            return;
        }
        bool isFastPush = false;
        int32_t pushCount = 0;
        do {
            if (videoPackets.empty()) {
                break;
            }
            auto& frame = videoPackets.front();
            frame->isFastPush = frame->isDiscard || stats_.fastPushDecodeCount > 0;
            isFastPush = frame->isFastPush;
            LogI("[decode] push video packet, dts:{}, pts:{}, video time:{}, push count:{}",
                 frame->dtsTime(),
                 frame->ptsTime(),
                 videoTime,
                 pushCount + 1
            );
            videoDecodeComponent_->send(std::move(frame));
            videoPackets.pop_front();
            pushCount++;
            stats_.fastPushDecodeCount--;
        } while (isFastPush && pushCount < kMaxPushCount);
    });
}

void Player::Impl::pushAVFrameDecode() noexcept {
    if (info_.hasAudio) {
        pushAudioPacketDecode();
    }
    if (info_.hasVideo) {
        pushVideoPacketDecode();
    }
}

void Player::Impl::pushVideoFrameToRender() noexcept {
    if (!videoDecodeComponent_) {
        return;
    }
    if (!stats_.isForceVideoRendered) {
        double diff = 0.0;
        if (info_.hasAudio && info_.hasVideo) {
            diff = audioRenderTime() - videoRenderTime();
            if (stats_.isAudioRenderEnd) {
                diff = 0;
            }
        } else if(!info_.hasVideo) {
            return;
        }
        if (diff < kAVSyncMinThreshold) {
            return;
        }
    }
    auto render = videoRender_.load();
    if (!render) {
        return;
    }
    AVFrameRefPtr framePtr = nullptr;
    videoFrames_.withLock([&framePtr](auto& frames){
        if (!frames.empty()) {
            framePtr = std::move(frames.begin()->second);
            frames.erase(frames.begin());
        }
    });
    if (!framePtr) {
        if (videoDecodeComponent_->isDecodeCompleted() &&
            !stats_.isVideoRenderEnd) {
            //render end
            if (auto videoRender = videoRender_.load()) {
                videoRender->renderEnd();
            }
            stats_.isVideoRenderEnd = true;
        }
        return;
    }
    render->pushVideoFrameRender(framePtr);
    auto renderTime = framePtr->ptsTime();
    auto nowState = state();
    LogI("push video frame render time:{}, is force:{}, state:{}",
         renderTime, stats_.isForceVideoRendered, static_cast<int>(nowState));
    render->setTime(Time::TimePoint::fromSeconds(renderTime));
    if (!stats_.isForceVideoRendered) {
        return;
    }
    if (auto seekRequest = seekRequest_.load()) {
        seekRequest_.reset();
        LogI("seek done, cost time:{}", (Time::nowTimeStamp() - seekRequest->startTime).toMilliSeconds());
    }
    stats_.isForceVideoRendered = false;
    if (helper_->debugInfo.pushVideoRenderTime.point() == 0) {
        helper_->debugInfo.pushVideoRenderTime = Time::nowTimeStamp();
        helper_->debugInfo.printTimeDeltas();
    }
}

void Player::Impl::pushAudioFrameToRender() noexcept {
    if (!audioRender_ || !audioDecodeComponent_) {
        return;
    }
    bool isPushFrame = false;
    audioFrames_.withLock([&isPushFrame, this](auto& audioFrames) {
        if (audioFrames.empty()) {
            return;
        }
        if (audioRender_->send(audioFrames.front())) {
            LogI("push audio:{}", audioFrames.front()->ptsTime());
            audioFrames.pop_front();
            isPushFrame = true;
        }
    });
    if (!isPushFrame &&
        audioDecodeComponent_->isDecodeCompleted() &&
        audioRender_->isHungry() &&
        !stats_.isAudioRenderEnd) {
        //render end
        audioRender_->renderEnd();
        stats_.isAudioRenderEnd = true;
    }
}

void Player::Impl::pushAVFrameToRender() noexcept {
    if (info_.hasAudio) {
        pushAudioFrameToRender();
    }
    if (info_.hasVideo) {
        pushVideoFrameToRender();
    }
}

void Player::Impl::process() noexcept {
    handleEvent(receiver_->tryReceiveAll());
    if (isStopped_) {
        return;
    }
    auto nowState = state();
    demuxData();
    pushAVFrameDecode();
    if (stats_.isForceVideoRendered ||
        nowState == PlayerState::Playing) {
        pushAVFrameToRender();
    }
}

void Player::Impl::doPlay() noexcept {
    LogI("do play");
    if (!info_.isValid) {
        return;
    }
    if (seekRequest_.isValid()) {
        LogI("player is seeking.");
        stats_.resumeAfterBuffering = true;
        return;
    }
    ownerThread_->start();
    if (info_.hasAudio) {
        if (audioDecodeComponent_) {
            audioDecodeComponent_->start();
        }
        if (audioRender_) {
            audioRender_->start();
        }
    }
    if (info_.hasVideo) {
        if (videoDecodeComponent_) {
            videoDecodeComponent_->start();
        }
        if (auto render = videoRender_.load()) {
            render->start();
        }
    }
}

void Player::Impl::doPause() noexcept {
    LogI("do pause");
    if (!info_.isValid) {
        return;
    }
    if (info_.hasAudio) {
        if (audioDecodeComponent_) {
            audioDecodeComponent_->pause();
        }
        if (audioRender_) {
            audioRender_->pause();
        }
    }
    if (info_.hasVideo) {
        if (videoDecodeComponent_) {
            videoDecodeComponent_->pause();
        }
        if (auto render = videoRender_.load()) {
            render->pause();
        }
    }
}

void Player::Impl::doStop() noexcept {
    LogI("do stop start");
    if (dataProvider_) {
        dataProvider_->close();
        dataProvider_.reset();
    }
    if (info_.hasAudio) {
        audioDecodeComponent_->pause();
        audioDecodeComponent_->close();
        audioDecodeComponent_.reset();
        audioRender_->setPullDataFunc(nullptr);
        audioRender_->stop();
        audioRender_.reset();
    }
    if (info_.hasVideo) {
        videoDecodeComponent_->pause();
        videoDecodeComponent_->close();
        videoDecodeComponent_.reset();
        if (auto render = videoRender_.load()) {
            render->pause();
        }
    }

    demuxerComponent_.reset();
    if (ownerThread_) {
        ownerThread_->stop();
    }
    GLContextManager::shareInstance().removeMainContext(playerId_);
    isReleased_ = true;
    cond_.notify_all();
    LogI("do stop end");
}

void Player::Impl::doSeek(
    PlayerSeekRequest seekRequest
) noexcept {
    if (!demuxerComponent_ || isStopped_ || isReleased_) {
        return;
    }
    
    constexpr double kSeekThreshold = 0.1;
    auto seekTime = seekRequest.seekTime;
    auto playedTime = currentPlayedTime();
    if (!seekRequest.isAccurate &&
        (isEqual(playedTime, seekTime, kSeekThreshold) || seekRequest_.isValid())
        ) {
        LogI("discard seek:{}", seekRequest.seekTime);
        return;
    }

    auto nowState = state();
    setState(PlayerState::Pause);
    if (nowState == PlayerState::Playing) {
        stats_.resumeAfterBuffering = true;
    }
    seekRequest.startTime = Time::nowTimeStamp();
    seekRequest_.reset(std::make_shared<PlayerSeekRequest>(seekRequest));
    LogI("resumeAfterSeek:{}", stats_.resumeAfterBuffering);
    setState(PlayerState::Buffering);
    auto demuxedTime = demuxedDuration();
    auto videoInfo = demuxerComponent_->videoInfo();
    LogI("[seek info]seek to time:{}, playedTime:{}, demuxedTime:{}", seekTime, playedTime, demuxedTime);
    if (playedTime <= seekTime && seekTime <= demuxedTime) {
        if (info_.hasVideo) {
            videoFrames_.withLock([seekTime](auto& frames){
                while(!frames.empty() && frames.begin()->second->ptsTime() < seekTime) {
                    LogI("discard video frame:{}", frames.begin()->second->ptsTime());
                    frames.erase(frames.begin());
                }
            });
            if (helper_->seekToLastAvailableKeyframe(seekTime)) {
                if (audioDecodeComponent_) {
                    audioDecodeComponent_->flush();
                }
                videoDecodeComponent_->flush();
            }
        }
        
        if (info_.hasAudio) {
            audioDecodeComponent_->flush();
            audioRender_->flush();
            audioFrames_.withLock([seekTime](auto& queue){
                while(!queue.empty() && queue.front()->ptsTime() < seekTime) {
                    queue.pop_front();
                }
            });

            audioPackets_.withLock([seekTime](auto& audioPackets) {
                while(!audioPackets.empty() && audioPackets.front()->ptsTime() < seekTime) {
                    audioPackets.pop_front();
                }
            });
        }
        LogI("seek to time:{}", seekTime);
    } else {
        if (dataProvider_) {
            dataProvider_->pause();
            dataList_.withLock([](auto& dataList) {
                dataList.clear(); //clear data list when seek
            });
        }
        if (demuxerComponent_) {
            demuxerComponent_->pause();
            demuxerComponent_->flush();
        }
        if (info_.hasAudio) {
            audioDecodeComponent_->flush();
            audioRender_->flush();
        }
        if (info_.hasVideo) {
            videoDecodeComponent_->flush();
        }
        audioPackets_.withLock([](auto& audioPackets){
            audioPackets.clear();
        });
        videoPackets_.withLock([](auto& videoPackets){
            videoPackets.clear();
        });
        audioFrames_.withLock([](auto& frames) {
            frames.clear();
        });
        videoFrames_.withLock([](auto& frames) {
            frames.clear();
        });
        auto seekPos = demuxerComponent_->getSeekToPos(seekTime);
        dataProvider_->seek(seekPos);
        demuxerComponent_->seekToPos(seekPos);
        LogI("long distance seek pos:{}, time:{}", seekPos, seekTime);
        dataProvider_->start();
        demuxerComponent_->start();
        stats_.reset();
        stats_.setSeekTime(seekTime);
    }
    if (info_.hasAudio) {
        audioRender_->seek(seekTime);
    }

    if (info_.hasVideo) {
        setVideoRenderTime(seekTime);
        stats_.setFastPush();
    } else {
        seekRequest_.reset(); //If there is only audio, seek is complete
    }
}

void Player::Impl::clearData() noexcept {
    if (info_.hasAudio) {
        audioPackets_.withLock([](auto& audioPackets){
            audioPackets.clear();
        });
        audioFrames_.withLock([](auto& frames){
            frames.clear();
        });
        audioDecodeComponent_->pause();
        audioDecodeComponent_->flush();
    }
    if (info_.hasVideo) {
        videoPackets_.withLock([](auto& videoPackets){
            videoPackets.clear();
        });
        videoFrames_.withLock([](auto& frames){
            frames.clear();
        });
        videoDecodeComponent_->pause();
        videoDecodeComponent_->flush();
    }
    if (demuxerComponent_ && dataProvider_) {
        auto seekPos = demuxerComponent_->getSeekToPos(0);
        dataProvider_->seek(seekPos);
        dataProvider_->pause();
        demuxerComponent_->seekToPos(seekPos);
        if (audioRender_) {
            audioRender_->seek(0.0);
        }
        if (auto render = videoRender_.load()) {
            render->setTime(Time::TimePoint::fromSeconds(0.0));
        }
    }
    seekRequest_.reset();
    stats_.reset();
}

void Player::Impl::doLoop() noexcept {
    if (info_.hasAudio) {
        if (audioDecodeComponent_) {
            audioDecodeComponent_->start();
        }

        if (audioRender_) {
            audioRender_->start();
        }
    }

    if (info_.hasVideo) {
        videoDecodeComponent_->start();
        if (auto render = videoRender_.load()) {
            render->start();
        }
    }
    dataProvider_->start();
}

void Player::Impl::setState(
    PlayerState state
) noexcept {
    bool isChanged = false;
    state_.withWriteLock([state, &isChanged](auto& nowState){
        if (nowState == state) {
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

std::expected<PlayerState, bool> getStateFromEvent(
    EventType event
) {
    switch (event) {
        case EventType::Play:
            return PlayerState::Playing;
        case EventType::Pause:
            return PlayerState::Pause;
        case EventType::Stop:
            return PlayerState::Stop;
        case EventType::ReadError:
        case EventType::DecodeError:
        case EventType::DemuxError:
        case EventType::RenderError:
            return PlayerState::Error;
        case EventType::Unknown:
            return PlayerState::Unknown;
        default:
            return std::unexpected(false);
    }
}

void Player::Impl::handleSettingUpdate(
    Event& t
) noexcept {
    if (t.type == EventType::UpdateSettingVolume) {
        auto volume = std::any_cast<float>(t.data);
        if (audioRender_) {
            audioRender_->setVolume(volume / 100.0f);
        }
        params_.withWriteLock([volume](auto& p){
            p->setting.volume = volume;
        });
        LogI("set volume:{}", volume);
    } else if (t.type == EventType::UpdateSettingMute) {
        auto isMute = std::any_cast<bool>(t.data);
        if (audioRender_) {
            if (isMute) {
                audioRender_->setVolume(0);
            } else {
                float volume;
                params_.withReadLock([&volume](auto& p){
                    volume = p->setting.volume;
                });
                audioRender_->setVolume(volume);
            }
        }
        LogI("set mute:{}", isMute);
    }
}

void Player::Impl::handleEvent(
    std::list<EventPtr>&& events
) noexcept {
    PlayerState changeState = PlayerState::Unknown;
    auto currentState = state();
    for (auto& event : events) {
        if (!event) {
            continue;
        }
        if (event->type == EventType::Seek) {
            auto seekRequest = std::any_cast<PlayerSeekRequest>(event->data);
            doSeek(seekRequest);
        } else if (event->type == EventType::Prepared) {
            preparePlayerInfo();
        } else if (EventType::UpdateSetting < event->type && event->type < EventType::UpdateSettingEnd) {
            handleSettingUpdate(*event);
        } else if (auto state = getStateFromEvent(event->type); state.has_value()) {
            LogI("eventType:{}", EventTypeToString(event->type));
            if (currentState == PlayerState::Stop || currentState == PlayerState::Error) {
                LogI("ignore event:{}", EventTypeToString(event->type));
                continue;
            }
            changeState = state.value();
        }
    }

    if (currentState == PlayerState::Playing && helper_->isRenderEnd()) {
        notifyPlayedTime(true); //notify time to end
        LogI("play end.");
        clearData();
        bool isLoop = false;
        params_.withReadLock([&isLoop](auto& p){
            isLoop = p->setting.isLoop;
        });
        if (!isLoop) {
            changeState = PlayerState::Completed;
            ownerThread_->pause(); //pause owner thread
        } else {
            doLoop();
        }
    }
    if (changeState != PlayerState::Unknown) {
        setState(changeState);
    }
}

void Player::Impl::notifyPlayerEvent(
    PlayerEvent event,
    std::string value
) noexcept {
    observer_.withReadLock([this, event, value = std::move(value)](auto& observer){
        if (auto ptr = observer.lock(); ptr) {
            ptr->notifyPlayerEvent(playerId_, event, value);
        }
    });
}

void Player::Impl::notifyPlayerState(
    PlayerState state
) noexcept {
    observer_.withReadLock([this, state](auto& observer){
        if (auto ptr = observer.lock(); ptr) {
            ptr->notifyPlayerState(playerId_, state);
        }
    });
}

void Player::Impl::notifyPlayedTime(bool isEndTime) noexcept {
    double time = 0.0;
    if (!isEndTime) {
        //Here, we specifically do not use the function currentPlayTime,
        //but use render time to print the audio and video synchronization time difference
        if (info_.hasAudio && info_.hasVideo) {
            auto videoTime = videoRenderTime();
            auto audioTime = audioRenderTime();
            time = std::min(videoTime, audioTime);
            time = std::min(time, info_.duration);
            LogI("notifyTime:{}, video time:{}, audio time:{}", time, videoTime, audioTime);
        } else if (info_.hasAudio) {
            time = audioRenderTime();
            time = std::min(time, info_.duration);
            LogI("notifyTime:{} (audio)", time);
        } else if (info_.hasVideo) {
            time = videoRenderTime();
            time = std::min(time, info_.duration);
            LogI("notifyTime:{} (video)", time);
        }
        if (stats_.lastNotifyPlayedTime > 0 &&
            isEqual(stats_.lastNotifyPlayedTime, time, 0.1)) {
            return;
        }
    } else {
        time = info_.duration;
    }
    stats_.lastNotifyPlayedTime = time;
    observer_.withReadLock([this, time](auto& observer){
        if (auto ptr = observer.lock(); ptr) {
            ptr->notifyPlayedTime(playerId_, time);
        }
    });
}

void Player::Impl::notifyCacheTime() noexcept {
    double time = demuxedDuration();
    if (isEqual(stats_.lastNotifyCacheTime, time, 0.1)) {
        return;
    }
    stats_.lastNotifyCacheTime = time;
    notifyPlayerEvent(PlayerEvent::UpdateCacheTime, std::to_string(time));
    LogI("notify cache time:{}", time);
}

PlayerParams Player::Impl::params() noexcept {
    PlayerParams params;
    params_.withReadLock([&params](auto& p){
        params = *p;
    });
    return params;
}

void Player::Impl::setLoop(
    bool isLoop
) noexcept {
    params_.withWriteLock([isLoop](auto& p){
        p->setting.isLoop = isLoop;
    });
    ownerThread_->start();
}

void Player::Impl::setVolume(
    float volume
) noexcept {
    if (!sender_) {
        LogE("error!! not init");
        return;
    }
    auto ptr = buildEvent(EventType::UpdateSettingVolume);
    ptr->data = std::make_any<float>(volume);
    sender_->send(std::move(ptr));
    ownerThread_->start();
}

void Player::Impl::setMute(
    bool isMute
) noexcept {
    if (!sender_) {
        LogE("error!! not init");
        return;
    }
    auto ptr = buildEvent(EventType::UpdateSettingMute);
    ptr->data = std::make_any<bool>(isMute);
    sender_->send(std::move(ptr));
    ownerThread_->start();
}

PlayerState Player::Impl::state() noexcept {
    PlayerState state = PlayerState::NotInited;
    state_.withReadLock([&state](auto& nowState){
        state = nowState;
    });
    return state;
}

double Player::Impl::currentPlayedTime() noexcept {
    if (info_.hasAudio && info_.hasVideo) {
        return std::min(audioRenderTime(), videoRenderTime());
    } else if (info_.hasAudio) {
        return audioRenderTime();
    } else if (info_.hasVideo) {
        return videoRenderTime();
    }
    return 0;
}

double Player::Impl::demuxedDuration() const noexcept {
    if (info_.hasAudio && info_.hasVideo) {
        return std::max(stats_.audioDemuxedTime, stats_.videoDemuxedTime);
    } else if (info_.hasAudio) {
        return stats_.audioDemuxedTime;
    } else if (info_.hasVideo) {
        return stats_.videoDemuxedTime;
    }
    return 0;
}

void Player::Impl::checkCacheState() noexcept {
    if (!dataProvider_ || !ownerThread_ || isStopped_ || !demuxerComponent_) {
        return;
    }
    auto nowState = state();
    if (nowState == PlayerState::Completed) {
        dataProvider_->pause();
        ownerThread_->pause();
        return;
    }
    PlayerSetting setting;
    params_.withReadLock([&setting](auto& p){
        setting = p->setting;
    });
    auto cachedDuration = demuxedDuration();
    auto playedTime = currentPlayedTime();
    auto cacheTime = cachedDuration - playedTime;
    cacheTime = std::max(0.0, cacheTime);
    if (nowState == PlayerState::Playing &&
        isEqualOrGreater(kMinCanPlayTime, cacheTime, 0.1)) {
        if (!dataProvider_->isCompleted()) {
            if (dataProvider_->isRunning()) {
                dataProvider_->start();
                LogI("read start");
            }
            setState(PlayerState::Buffering);
            stats_.resumeAfterBuffering = true;
            LogI("buffering !!!, cache time is enough:{}", cacheTime);
        }
        if (demuxerComponent_ && !demuxerComponent_->isRunning()) {
            demuxerComponent_->start();
        }
        if (!ownerThread_->isRunning()) {
            ownerThread_->start();
        }
        LogI("unable to continue playing:{}", cacheTime);
    } else if (nowState == PlayerState::Buffering) {
        if (isEqualOrGreater(cacheTime, kMinCanPlayTime, 0.1) ||
            isEqualOrGreater(cachedDuration, info_.duration, 0.1) ||
            demuxerComponent_->isCompleted()) {
            setState(stats_.resumeAfterBuffering ? PlayerState::Playing : PlayerState::Ready);
            LogI("buffering end, cache time is enough:{}, playing:{}", cacheTime, stats_.resumeAfterBuffering);
            stats_.resumeAfterBuffering = false;
        }
    }

    LogI("demux cache time:{}, demuxedDuration:{} played time:{}", cacheTime, cachedDuration, playedTime);
    if (isEqualOrGreater(setting.minCacheTime, cacheTime, 0.1)) {
        if (!dataProvider_->isCompleted()) {
            dataProvider_->start();
            LogI("cache time is too small:{:.2f}", cacheTime);
        }
        
        if (!demuxerComponent_->isRunning() && !demuxerComponent_->isCompleted()) {
            demuxerComponent_->start();
        }
        
        if (!ownerThread_->isRunning()) {
            ownerThread_->start();
        }
        
    } else if (isEqualOrGreater(cacheTime, setting.maxCacheTime)) {
        if (dataProvider_) {
            dataProvider_->pause();
        }
        if (demuxerComponent_) {
            demuxerComponent_->pause();
        }
        LogI("read pause, cache time is too large:{:.2f}", cacheTime);
    }
    
    if (!dataProvider_->isRunning() && !demuxerComponent_->isRunning()) {
        static const std::vector<PlayerState> pauseStates = {
            PlayerState::Ready,
            PlayerState::Pause,
            PlayerState::Completed,
            PlayerState::Error
        };
        auto isPauseState = std::ranges::any_of(pauseStates, [nowState](PlayerState state) {
            return state == nowState;
        });
        if (isPauseState && receiver_->empty()) {
            ownerThread_->pause();
            LogI("owner pause");
        }
    }
}

double Player::Impl::videoRenderTime() noexcept {
    double videoTime = 0.0;
    if (auto render = videoRender_.load()) {
        videoTime = render->playedTime().second();
    }
    return videoTime;
}

double Player::Impl::audioRenderTime() noexcept {
    if (audioRender_) {
        return audioRender_->playedTime().second();
    }
    return 0;
}

bool Player::Impl::isVideoNeedDecode(
    double renderedTime
) noexcept {
    if (!videoDecodeComponent_) {
        return false;
    }
    bool isNeedDecode = false;
    videoPackets_.withLock([renderedTime, &isNeedDecode](auto& videoPackets) {
        if (!videoPackets.empty() &&
            isEqualOrGreater(renderedTime + kMinPushDecodeTime, videoPackets.front()->dtsTime())) {
            isNeedDecode = true;
        }
    });

    if (!isNeedDecode) {
        isNeedDecode = videoFrames_.withLock([](auto& frames) {
            LogI("[decode] cache video frame size: {}", frames.size());
            if (frames.size() >= kMaxCacheDecodedVideoFrame) {
                return false;
            }
            return true;
        });
    }

    return isNeedDecode && !videoDecodeComponent_->isFull();
}

bool Player::Impl::isAudioNeedDecode(
    double renderedTime
) noexcept {
    if (!audioDecodeComponent_) {
        return false;
    }
    bool isNeedDecode = false;
    audioPackets_.withLock([renderedTime, &isNeedDecode](auto& audioPackets) {
        if (!audioPackets.empty() &&
            isEqualOrGreater(renderedTime + kMinPushDecodeTime, audioPackets.front()->dtsTime())) {
            isNeedDecode = true;
        }
    });

    if (!isNeedDecode) {
        isNeedDecode = audioFrames_.withLock(
            [](auto& frames) {
                LogI("[decode] cache audio frame size: {}",
                     frames.size());
                if (frames.size() >= kMaxCacheDecodedAudioFrame) {
                    return false;
                }
                return true;
            }
        );
    }

    return isNeedDecode && !audioDecodeComponent_->isFull();
}

AVFrameRefPtr Player::Impl::requestRender() noexcept {
    double diff = 0;
    static int count = 0;
    LogI("requestRenderFrame: {}, {}", audioRenderTime(), videoRenderTime());
    if (info_.hasAudio && info_.hasVideo) {
        diff = audioRenderTime() - videoRenderTime();
        if (stats_.isAudioRenderEnd) {
            diff = 0;
        }
    } else if(!info_.hasVideo) {
        return nullptr;
    }

    bool isNeedCheckSync = true;
    if (abs(diff) >= kAVSyncMaxThreshold) {
        isNeedCheckSync = false;
    }
    
    if (isNeedCheckSync && diff < (-kAVSyncMinThreshold) && !stats_.isForceVideoRendered) {
        if (!demuxerComponent_) {
            LogE("demuxer component is nullptr");
            return nullptr;
        }
        auto videoInfo = demuxerComponent_->videoInfo();
        auto videoTime = videoRenderTime() - videoInfo->frameDuration();
        
        videoTime = videoTime < 0 ? 0 : videoTime;
        setVideoRenderTime(videoTime);
        return nullptr;
    }
    AVFrameRefPtr framePtr = nullptr;
    videoFrames_.withLock([&framePtr](auto& frames){
        if (!frames.empty()) {
            framePtr = std::move(frames.begin()->second);
            frames.erase(frames.begin());
        }
    });
    if (framePtr) {
        auto pts= framePtr->ptsTime();
        LogI("push video frame render:{}, pts:{}", pts, framePtr->pts);
        setVideoRenderTime(pts);
        stats_.isForceVideoRendered = false;
        count = 0;
    } else if (videoDecodeComponent_) {
        if (videoDecodeComponent_->isInputCompleted() &&
            videoDecodeComponent_->isDecodeCompleted() &&
            !stats_.isVideoRenderEnd) {
            //render end
            if (auto videoRender = videoRender_.load()) {
                videoRender->renderEnd();
            }
            stats_.isVideoRenderEnd = true;
            count = 0;
        } else {
            count++;
            LogI("no video frame to render:{}", count);
        }

    }
    return framePtr;
}

void Player::Impl::setRenderImpl(
    std::weak_ptr<IVideoRender> render
) noexcept {
    videoRender_.store(std::move(render));
    auto ptr = videoRender_.load();
    if (ptr) {
        auto weak = weak_from_this();
        ptr->setRequestRenderFunc([weak = std::move(weak)]() -> AVFrameRefPtr{
            auto self = weak.lock();
            if (!self || self->isStopped_) {
                return nullptr;
            }
            return self->requestRender();
        });
    }
}

void Player::Impl::setVideoRenderTime(
    double time
) noexcept {
    if (auto render = videoRender_.load()) {
        render->setTime(Time::TimePoint::fromSeconds(time));
    }
}

}//end namespace slark
