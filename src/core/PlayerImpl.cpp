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

namespace slark {

const double kAVSyncMinThreshold = 0.1; //100ms
const double kAVSyncMaxThreshold = 10; //10s
const uint32_t kMaxCacheDecodedVideoFrame = 10;
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
    updateState(PlayerState::Stop);
    std::unique_lock<std::mutex> lock(releaseMutex_);
    cond_.wait(lock, [this]{
        return isReleased_.load();
    });
}

void Player::Impl::seek(double time, bool isAccurate) noexcept {
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
    sender_->send(buildEvent(state));
    ownerThread_->start();
}

void Player::Impl::addObserver(IPlayerObserverPtr observer) noexcept {
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
    
    auto readDataCallback = [this](DataPacket data, IOState state) {
        LogI("receive data offset:{} size: {}", data.offset, data.length());
        if (data.data) {
            dataList_.withLock([&](auto& dataList) {
                dataList.emplace_back(std::move(data));
            });
        }
        
        if (state == IOState::Error) {
            sender_->send(buildEvent(EventType::ReadError));
        }
    };

    if (!PlayerImplHelper::createDataProvider(path, this, std::move(readDataCallback))) {
        setState(PlayerState::Error);
        notifyPlayerEvent(PlayerEvent::OnError, std::to_string(static_cast<uint8_t>(PlayerErrorCode::OpenFileError)));
        return false;
    }
    return true;
}

bool Player::Impl::createDemuxer(DataPacket& data) noexcept {
    if (!helper_->appendProbeData(static_cast<uint64_t>(data.offset), std::move(data.data))) {
        return false;
    }
    auto dataView = helper_->probeView();
    auto demuxerType = DemuxerManager::shareInstance().probeDemuxType(dataView);
    if (demuxerType == DemuxerType::Unknown) {
        return false;
    }

    demuxer_ = DemuxerManager::shareInstance().create(demuxerType);
    if (!demuxer_) {
        LogE("not found demuxer type:{}", static_cast<int32_t>(demuxerType));
        return false;
    } else {
        DemuxerConfig config;
        config.fileSize = dataProvider_->size();
        params_.withReadLock([&config](auto& params){
            config.filePath = params->item.path;
        });
        demuxer_->init(std::move(config));
        helper_->debugInfo.createdDemuxerTime = Time::nowTimeStamp();
    }
    return true;
}

bool Player::Impl::openDemuxer(DataPacket& data) noexcept {
    if (!helper_->appendProbeData(static_cast<uint64_t>(data.offset), std::move(data.data))) {
        return false;
    }
    if (demuxer_ == nullptr) {
        return false;
    }
    auto res = helper_->openDemuxer();
    if (res) {
        LogI("open demuxer success.");
        helper_->debugInfo.openedDemuxerTime = Time::nowTimeStamp();
        preparePlayerInfo();
    }
    return res;
}

void Player::Impl::createAudioComponent(const PlayerSetting& setting) noexcept {
    auto audioInfo = demuxer_->audioInfo();
    auto decodeType = DecoderManager::shareInstance().getDecoderType(audioInfo->mediaInfo, setting.enableAudioSoftDecode);
    if (!DecoderManager::shareInstance().contains(decodeType)) {
        LogE("not found decoder:media info {}", audioInfo->mediaInfo);
        return;
    }
    audioDecodeComponent_ = std::make_shared<DecoderComponent>([this](auto frame) {
        if (isStopped_) {
            return;
        }

        if (seekRequest_.has_value() && frame->ptsTime() < seekRequest_.value().seekTime) {
            LogI("discard decoded audio frame pts:{}", frame->ptsTime());
            return;
        }
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
    audioRender_->firstFrameRenderCallBack = [this](Time::TimePoint timestamp){
        if (isStopped_) {
            return;
        }
    };
    helper_->debugInfo.createAudioRenderTime = Time::nowTimeStamp();
    LogI("create audio render success");
}

void Player::Impl::createVideoComponent(const PlayerSetting& setting) noexcept  {
    auto decodeType = DecoderManager::shareInstance().getDecoderType(demuxer_->videoInfo()->mediaInfo, setting.enableVideoSoftDecode);
    if (!DecoderManager::shareInstance().contains(decodeType)) {
        LogE("not found decoder:media info {}", demuxer_->videoInfo()->mediaInfo);
        return;
    }
    videoDecodeComponent_ = std::make_shared<DecoderComponent>([this](auto frame) {
        if (isStopped_) {
            return;
        }
        if (seekRequest_.has_value() && frame->ptsTime() < seekRequest_.value().seekTime) {
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
        LogI("create video decoder error:{}", demuxer_->videoInfo()->mediaInfo);
        return;
    }
    auto config = std::make_shared<VideoDecoderConfig>();
    config->playerId = playerId_;
    config->initWithVideoInfo(demuxer_->videoInfo());
    if (videoDecodeComponent_->open(decodeType, config)) {
        LogI("create video decode component success");
    } else {
        return;
    }
    helper_->debugInfo.openedVideoDecoderTime = Time::nowTimeStamp();
    if (auto render = videoRender_.load()) {
        render->notifyVideoInfo(demuxer_->videoInfo());
    }
}

void Player::Impl::preparePlayerInfo() noexcept {
    //prepare player info, no lock is added here because it is only changed once
    {
        info_.isValid = true;
        info_.hasAudio = demuxer_->hasAudio();
        info_.hasVideo = demuxer_->hasVideo();
        info_.duration = demuxer_->totalDuration().second();
    }
    setState(PlayerState::Prepared);
    PlayerSetting setting;
    params_.withReadLock([&setting](auto& p){
        setting = p->setting;
    });
    if (info_.hasAudio) {
        LogI("has audio, audio info:{}", demuxer_->audioInfo()->mediaInfo);
        createAudioComponent(setting);
        LogI("init player success 0.");
    } else {
        LogI("no audio");
    }
    
    if (info_.hasVideo) {
        LogI("has video, video info:{}", demuxer_->videoInfo()->mediaInfo);
        createVideoComponent(setting);
        LogI("init player success 1.");
    } else {
        LogI("no video");
    }
    LogI("init player success 2.");
    doPause();
    if (info_.hasVideo) {
        dataProvider_->start(); //render first frame
        ownerThread_->start();
        stats_.isForceVideoRendered = true;
    }
    LogI("init player success.");
    setState(PlayerState::Buffering);
}

void Player::Impl::handleAudioPacket(AVFramePtrArray& audioPackets) noexcept {
    for (auto& packet : audioPackets) {
        auto pts = packet->ptsTime();
        stats_.audioDemuxedTime = pts + packet->duration / 1000.0;
        if (demuxer_->isCompleted()) {
            stats_.audioDemuxedTime = info_.duration;
        }
        if (seekRequest_.has_value() && pts < seekRequest_.value().seekTime) {
            LogI("discard audio frame:{}", packet->ptsTime());
            continue;
        }
        LogI("demux audio frame:{}, pts:{}, dts:{}", packet->index, packet->dtsTime(), packet->ptsTime());
        audioPackets_.push_back(std::move(packet));
    }
}

void Player::Impl::handleVideoPacket(AVFramePtrArray& videoPackets) noexcept {
    for (auto& packet : videoPackets) {
        auto pts = packet->ptsTime();
        stats_.videoDemuxedTime = pts + packet->duration / 1000.0;
        if (demuxer_->isCompleted()) {
            stats_.videoDemuxedTime = info_.duration;
        }
        //fix atomic
        if (seekRequest_.has_value() && pts < seekRequest_.value().seekTime) {
            if (!demuxer_->isCompleted()) {
                packet->isDiscard = true;
                LogI("[seek info]discard video frame:{}, time:{}", pts, seekRequest_.value().seekTime);
            }
        }
        LogI("demux video frame:{}, pts:{}, dts:{}, seek:{}", packet->index, pts, packet->dtsTime(),
             seekRequest_.has_value() ? seekRequest_.value().seekTime : 0);
        videoPackets_.push_back(std::move(packet));
    }
}

void Player::Impl::demuxData() noexcept {
    if (demuxer_ && demuxer_->isCompleted()) {
        return;
    }
    std::vector<DataPacket> dataList;
    constexpr uint32_t kMaxHandleDataSize = 5;//5 * 64kb
    constexpr uint32_t kMaxCacheDataSize = 20;//20 * 64kb
    uint32_t cacheSize = 0;
    dataList_.withLock([&](auto& list) {
        auto size = std::min(kMaxHandleDataSize, uint32_t(list.size()));
        dataList.insert(dataList.end(),
                        std::make_move_iterator(list.begin()),
                        std::make_move_iterator(list.begin() + size));
        list.erase(list.begin(), list.begin() + size);
        cacheSize = static_cast<uint32_t>(list.size());
    });
    if (dataList.empty()) {
        return;
    }
    //LogI("demux data size:{}", dataList.size());
    for (auto& data : dataList) {
        if (demuxer_ == nullptr && !createDemuxer(data)) {
            LogI("create demuxer failed.");
            continue;
        }

        if (!demuxer_->isOpened() && !openDemuxer(data)) {
            continue;
        }
        auto parseResult = demuxer_->parseData(data);
        //LogI("demux frame count %d, state: %d", frameList.size(), code);
        if (parseResult.resultCode == DemuxerResultCode::Failed && dataList.empty()) {
            break;
        }
        if (parseResult.resultCode == DemuxerResultCode::ParsedHeader) {
            preparePlayerInfo();
        } else if (parseResult.resultCode == DemuxerResultCode::ParsedFPS) {
            if (auto render = videoRender_.load()) {
                render->notifyVideoInfo(demuxer_->videoInfo());
            }
        }
        if (!parseResult.audioFrames.empty()) {
            handleAudioPacket(parseResult.audioFrames);
        }
        if (!parseResult.videoFrames.empty()) {
            handleVideoPacket(parseResult.videoFrames);
        }
        if (isStopped_) {
            break;
        }
    }
}

void Player::Impl::pushAudioPacketDecode() noexcept {
    if (!audioDecodeComponent_) {
        return;
    }
    if (audioPackets_.empty()) {
        if (demuxer_->isCompleted() && !audioDecodeComponent_->isInputCompleted()) {
            audioDecodeComponent_->setInputCompleted();
            LogI("input audio completed");
        }
        return;
    }
    auto audioClockTime = audioRenderTime();
    if (!isAudioNeedDecode(audioClockTime)) {
        return;
    }
    auto packet = std::move(audioPackets_.front());
    LogI("[decode] push audio frame pushFrameDecode, dts:{}, audio time:{}", packet->dtsTime(), audioClockTime);
    audioDecodeComponent_->send(std::move(packet));
    audioPackets_.pop_front();
}

void Player::Impl::pushVideoPacketDecode(bool ) noexcept {
    if (!videoDecodeComponent_) {
        return;
    }
    LogI("videoPackets size:{}", videoPackets_.size());
    if (videoPackets_.empty()) {
        if (demuxer_->isCompleted() && !videoDecodeComponent_->isInputCompleted()) {
            videoDecodeComponent_->setInputCompleted();
            LogI("input video completed");
        }
        return;
    }
    auto videoTime = videoRenderTime();
    auto& frame = videoPackets_.front();
    if (stats_.isForceVideoRendered || isVideoNeedDecode(videoTime)) {
        LogI("[decode] push video packet, dts:{}, pts:{}, video time:{}",
             frame->dtsTime(),
             frame->ptsTime(),
             videoTime
        );
        videoDecodeComponent_->send(std::move(frame));
        videoPackets_.pop_front();
    }
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
    if (!stats_.isForceVideoRendered) {
        double diff = 0.0;
        if (info_.hasAudio && info_.hasVideo) {
            diff = audioRenderTime() - videoRenderTime();
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
        if (videoDecodeComponent_->isInputCompleted() &&
            !stats_.isVideoRenderEnd &&
            videoDecodeComponent_->isDecodeCompleted()) {
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
    if (seekRequest_.has_value()) {
        if (seekRequest_.value().isAccurate && resumeAfterSeek_) {
            setState(PlayerState::Playing);
            resumeAfterSeek_ = false;
        } else {
            setState(PlayerState::Ready);
        }
        seekRequest_.reset();
    } else {
        setState(PlayerState::Ready);
    }
    stats_.isForceVideoRendered = false;
    if (helper_->debugInfo.pushVideoRenderTime.point() == 0) {
        helper_->debugInfo.pushVideoRenderTime = Time::nowTimeStamp();
        helper_->debugInfo.printTimeDeltas();
    }
}

void Player::Impl::pushAudioFrameToRender() noexcept {
    if (!audioRender_) {
        return;
    }
    audioFrames_.withLock([this](auto& audioFrames){
        if (audioFrames.empty()) {
            if (audioDecodeComponent_->isInputCompleted() &&
                !stats_.isAudioRenderEnd &&
                audioDecodeComponent_->isDecodeCompleted()) {
                //render end
                audioRender_->renderEnd();
                stats_.isAudioRenderEnd = true;
            }
            return;
        }
        if (audioRender_->send(audioFrames.front())) {
            LogI("push audio:{}", audioFrames.front()->ptsTime());
            audioFrames.pop_front();
        }
    });
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
    auto nowState = state();
    if (nowState == PlayerState::Stop) {
        return;
    }
    demuxData();
    pushAVFrameDecode();
    if (seekRequest_.has_value() ||
        nowState == PlayerState::Buffering ||
        nowState == PlayerState::Playing) {
        pushAVFrameToRender();
    }
}

void Player::Impl::doPlay() noexcept {
    LogI("do play");
    if (!info_.isValid) {
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

    demuxer_.reset();
    if (ownerThread_) {
        ownerThread_->stop();
    }
    GLContextManager::shareInstance().removeMainContext(playerId_);
    isReleased_ = true;
    cond_.notify_all();
    LogI("do stop end");
}

void Player::Impl::doSeek(PlayerSeekRequest seekRequest) noexcept {
    if (!demuxer_ || isStopped_ || isReleased_) {
        return;
    }
    
    constexpr double kSeekThreshold = 0.1;
    auto seekTime = seekRequest.seekTime;
    auto playedTime = currentPlayedTime();
    if (!seekRequest.isAccurate && (isEqual(playedTime, seekTime, kSeekThreshold) || seekRequest_.has_value())) {
        LogI("discard seek:{}", seekRequest.seekTime);
        return;
    }

    auto nowState = state();
    setState(PlayerState::Pause);
    if (nowState == PlayerState::Playing) {
        resumeAfterSeek_ = true;
    }
    seekRequest_ = seekRequest;
    LogI("resumeAfterSeek:{}", resumeAfterSeek_);
    setState(PlayerState::Buffering);

    stats_.reset();
    auto demuxedTime = demuxedDuration();
    auto videoInfo = demuxer_->videoInfo();
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
            audioFrames_.withLock([seekTime](auto& queue){
                while(!queue.empty() && queue.front()->ptsTime() < seekTime) {
                    queue.pop_front();
                }
            });

            while(!audioPackets_.empty() && audioPackets_.front()->ptsTime() < seekTime) {
                audioPackets_.pop_front();
            }
        }
        LogI("seek to time:{}", seekTime);
    } else {
        auto seekPos = demuxer_->getSeekToPos(seekTime);
        if (audioDecodeComponent_) {
            audioDecodeComponent_->flush();
        }
        if (videoDecodeComponent_) {
            videoDecodeComponent_->flush();
        }
        audioPackets_.clear();
        videoPackets_.clear();
        audioFrames_.withLock([](auto& frames) {
            frames.clear();
        });
        videoFrames_.withLock([](auto& frames) {
            frames.clear();
        });
        dataProvider_->seek(seekPos);
        demuxer_->seekPos(seekPos);
        LogI("long distance seek pos:{}, time:{}", seekPos, seekTime);
        dataProvider_->start();
        stats_.setSeekTime(seekTime);
    }
    audioRender_->seek(seekTime);
    setVideoRenderTime(seekTime);
    stats_.isForceVideoRendered = true;
}

void Player::Impl::clearData() noexcept {
    if (info_.hasAudio) {
        audioPackets_.clear();
        audioFrames_.withLock([](auto& frames){
            frames.clear();
        });
        audioDecodeComponent_->pause();
        audioDecodeComponent_->flush();
    }
    if (info_.hasVideo) {
        videoPackets_.clear();
        videoFrames_.withLock([](auto& frames){
            frames.clear();
        });
        videoDecodeComponent_->pause();
        videoDecodeComponent_->flush();
    }
    if (demuxer_ && dataProvider_) {
        auto seekPos = demuxer_->getSeekToPos(0);
        dataProvider_->seek(seekPos);
        dataProvider_->pause();
        demuxer_->seekPos(seekPos);
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

void Player::Impl::setState(PlayerState state) noexcept {
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
    } else if (state == PlayerState::Stop) {
        doStop();
    }
    notifyPlayerState(state);
}

std::expected<PlayerState, bool> getStateFromEvent(EventType event) {
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
            return PlayerState::Pause;
        case EventType::Unknown:
            return PlayerState::Unknown;
        default:
            return std::unexpected(false);
    }
}

void Player::Impl::handleSettingUpdate(Event& t) noexcept {
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
    } else if (t.type == EventType::UpdateSettingRenderSize) {
        auto size = std::any_cast<std::pair<uint32_t, uint32_t>>(t.data);
        params_.withWriteLock([size](auto& p){
            p->setting.width = size.first;
            p->setting.height = size.second;
        });
        LogI("set size, width:{}, height:{}", size.first, size.second);
    }
}

void Player::Impl::handleEvent(std::list<EventPtr>&& events) noexcept {
    PlayerState changeState = PlayerState::Unknown;
    for (auto& event : events) {
        if (!event) {
            continue;
        }
        if (event->type == EventType::Seek) {
            auto seekRequest = std::any_cast<PlayerSeekRequest>(event->data);
            doSeek(seekRequest);
        } else if (EventType::UpdateSetting < event->type && event->type < EventType::UpdateSettingEnd) {
            handleSettingUpdate(*event);
        } else if (auto state = getStateFromEvent(event->type); state.has_value()) {
            LogI("eventType:{}", EventTypeToString(event->type));
            if (changeState != PlayerState::Stop) {
                changeState = state.value();
            } else {
                LogI("player stopped.");
            }
        }
    }
    auto currentState = state();
    if (currentState == PlayerState::Playing && helper_->isRenderEnd()) {
        notifyPlayedTime(); //notify time to end
        LogI("play end.");
        clearData();
        bool isLoop = false;
        params_.withReadLock([&isLoop](auto& p){
            isLoop = p->setting.isLoop;
        });
        if (!isLoop) {
            changeState = PlayerState::Completed;
        } else {
            doLoop();
        }
    }
    if (changeState != PlayerState::Unknown) {
        setState(changeState);
    }
}

void Player::Impl::notifyPlayerEvent(PlayerEvent event, std::string value) noexcept {
    observer_.withReadLock([this, event, value = std::move(value)](auto& observer){
        if (auto ptr = observer.lock(); ptr) {
            ptr->notifyPlayerEvent(playerId_, event, value);
        }
    });
}

void Player::Impl::notifyPlayerState(PlayerState state) noexcept {
    observer_.withReadLock([this, state](auto& observer){
        if (auto ptr = observer.lock(); ptr) {
            ptr->notifyPlayerState(playerId_, state);
        }
    });
}

void Player::Impl::notifyPlayedTime() noexcept {
    double time = 0.0;
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
    if (isEqual(stats_.lastNotifyPlayedTime, time, 0.1)) {
        return;
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
}

PlayerParams Player::Impl::params() noexcept {
    PlayerParams params;
    params_.withReadLock([&params](auto& p){
        params = *p;
    });
    return params;
}

void Player::Impl::setLoop(bool isLoop) {
    params_.withWriteLock([isLoop](auto& p){
        p->setting.isLoop = isLoop;
    });
}

void Player::Impl::setVolume(float volume) {
    auto ptr = buildEvent(EventType::UpdateSettingVolume);
    ptr->data = std::make_any<float>(volume);
    sender_->send(std::move(ptr));
}

void Player::Impl::setMute(bool isMute) {
    auto ptr = buildEvent(EventType::UpdateSettingMute);
    ptr->data = std::make_any<bool>(isMute);
    sender_->send(std::move(ptr));
}
 
void Player::Impl::setRenderSize(uint32_t width, uint32_t height) noexcept {
    auto ptr = buildEvent(EventType::UpdateSettingRenderSize);
    ptr->data = std::make_any<std::pair<uint32_t, uint32_t>>(width, height);
    sender_->send(std::move(ptr));
}

PlayerState Player::Impl::state() noexcept {
    PlayerState state = PlayerState::Unknown;
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
    if (!dataProvider_ || !ownerThread_ || isStopped_) {
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
    LogI("demux cache time:{}, demuxedDuration:{} played time:{}", cacheTime, cachedDuration, playedTime);
    auto startRead = [this](bool isStart){
        auto isRunning = dataProvider_->isRunning();
        if (!isStart && isRunning) {
            dataProvider_->pause();
            LogI("read pause");
        } else if (isStart && !isRunning) {
            dataProvider_->start();
            LogI("read resume");
        }
    };
    auto pauseOwnerThread = [this](PlayerState nowState) {
        if ((nowState == PlayerState::Ready || nowState == PlayerState::Pause) &&
            receiver_->empty()) {
            ownerThread_->pause();
            LogI("owner pause");
        }
    };
    if (cacheTime < setting.minCacheTime && !dataProvider_->isCompleted()) {
        startRead(true);
        ownerThread_->start();
        LogI("owner resume");
    } else if (cacheTime >= setting.maxCacheTime ||
        (info_.isValid && isEqualOrGreater(playedTime, info_.duration, 0.1)) ) {
        pauseOwnerThread(nowState);
        startRead(false);
    } else if (dataProvider_->isCompleted()) {
        startRead(false);
        if (demuxer_->isCompleted()) {
            pauseOwnerThread(nowState);
        }
    } else if (!dataProvider_->isRunning()) {
        pauseOwnerThread(nowState);
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

bool Player::Impl::isVideoNeedDecode(double renderedTime) noexcept {
    bool isNeedDecode = false;
    isNeedDecode = videoFrames_.withLock([](auto& frames) {
        LogI("[decode] video frame size: {}", frames.size());
        if (frames.size() >= kMaxCacheDecodedVideoFrame) {
            return false;
        }
        return true;
    });
    if (!isNeedDecode && !videoPackets_.empty() &&
        isEqualOrGreater(renderedTime + kMinPushDecodeTime, videoPackets_.front()->dtsTime())) {
        isNeedDecode = true;
    }

    return isNeedDecode;
}

bool Player::Impl::isAudioNeedDecode(double renderedTime) noexcept {
    bool isNeedDecode = audioFrames_.withLock([](auto& frames) {
        LogI("[decode] audio frame size: {}", frames.size());
        if (frames.size() >= kMaxCacheDecodedAudioFrame) {
            return false;
        }
        return true;
    });

    if (!isNeedDecode && !audioPackets_.empty() &&
        isEqualOrGreater(renderedTime + kMinPushDecodeTime, audioPackets_.front()->dtsTime())) {
        isNeedDecode = true;
    }
    return isNeedDecode;
}

AVFrameRefPtr Player::Impl::requestRender() noexcept {
    double diff = 0;
    LogI("requestRenderFrame: {}, {}", audioRenderTime(), videoRenderTime());
    if (info_.hasAudio && info_.hasVideo) {
        diff = audioRenderTime() - videoRenderTime();
    } else if(!info_.hasVideo) {
        return nullptr;
    }

    bool isNeedCheckSync = true;
    if (abs(diff) >= kAVSyncMaxThreshold) {
        isNeedCheckSync = false;
    }
    
    if (isNeedCheckSync && diff < (-kAVSyncMinThreshold) && !stats_.isForceVideoRendered) {
        auto videoInfo = demuxer_->videoInfo();
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
    } else {
        if (videoDecodeComponent_->isInputCompleted() &&
            videoDecodeComponent_->isDecodeCompleted() &&
            !stats_.isVideoRenderEnd) {
            //render end
            if (auto videoRender = videoRender_.load()) {
                videoRender->renderEnd();
            }
            stats_.isVideoRenderEnd = true;
        }
    }
    return framePtr;
}

void Player::Impl::setRenderImpl(std::weak_ptr<IVideoRender> render) noexcept {
    videoRender_.store(std::move(render));
    auto ptr = videoRender_.load();
    if (ptr) {
        ptr->setRequestRenderFunc([this]() -> AVFrameRefPtr{
            return requestRender();
        });
    }
}

void Player::Impl::setVideoRenderTime(double time) noexcept {
    if (auto render = videoRender_.load()) {
        render->setTime(Time::TimePoint::fromSeconds(time));
    }
}

}//end namespace slark
