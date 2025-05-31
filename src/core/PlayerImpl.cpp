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

namespace slark {

const long double kAVSyncMinThreshold = 0.1; //100ms
const long double kAVSyncMaxThreshold = 10; //10s
const uint32_t kMaxCacheDecodedVideoFrame = 3;
const uint32_t kMaxCacheDecodedAudioFrame = 10;

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

void Player::Impl::seek(long double time, bool isAccurate) noexcept {
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
    ownerThread_->runLoop(200ms, [this](){
        if (state() == PlayerState::Playing) {
            notifyPlayedTime();
        }
        if (!isStopped_) {
            notifyCacheTime();
        }
    });
    ownerThread_->runLoop(500ms, [this](){
        checkCacheState();
    });
    
    dataProvider_->start();
    ownerThread_->start();
    setState(PlayerState::Buffering);
    LogI("player buffering start.");
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
    helper_ = std::make_unique<PlayerImplHelper>(weak_from_this());
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
        initPlayerInfo();
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
        if (!stats_.isFirstAudioRendered) {
            stats_.audioDecodeDelta = frame->stats.decodedStamp - frame->stats.prepareDecodeStamp;
        }
        audioFrames_.withLock([&frame](auto& frames){
            frames.emplace_back(std::move(frame));
        });
    });
    if (!audioDecodeComponent_) {
        LogI("create audio pushFrameDecode Component error:{}", audioInfo->mediaInfo);
        return;
    }
    auto config = std::make_shared<AudioDecoderConfig>();
    config->playerId = playerId_;
    config->initWithAudioInfo(audioInfo);
    if (!audioDecodeComponent_->open(decodeType, config)) {
        LogI("create audio pushFrameDecode component success");
        return ;
    }
    audioRender_ = std::make_unique<AudioRenderComponent>(audioInfo);
    audioRender_->firstFrameRenderCallBack = [this](Time::TimePoint timestamp){
        stats_.isFirstAudioRendered = true;
        stats_.audioRenderDelta = timestamp - stats_.audioRenderDelta;
    };
    LogI("create audio render success");
}

void Player::Impl::createVideoComponent(const PlayerSetting& setting) noexcept  {
    auto decodeType = DecoderManager::shareInstance().getDecoderType(demuxer_->videoInfo()->mediaInfo, setting.enableVideoSoftDecode);
    if (!DecoderManager::shareInstance().contains(decodeType)) {
        LogE("not found decoder:media info {}", demuxer_->videoInfo()->mediaInfo);
        return;
    }
    videoDecodeComponent_ = std::make_shared<DecoderComponent>([this](auto frame) {
        LogI("pushFrameDecode video frame info:{}", frame->ptsTime());
        videoFrames_.withLock([&frame](auto& frames){
            frames.emplace_back(std::move(frame));
        });
    });
    if (!videoDecodeComponent_) {
        LogI("create video decoder error:{}", demuxer_->videoInfo()->mediaInfo);
        return;
    }
    auto config = std::make_shared<VideoDecoderConfig>();
    config->playerId = playerId_;
    config->initWithVideoInfo(demuxer_->videoInfo());

    if (videoDecodeComponent_->open(decodeType, config)) {
        LogI("create video pushFrameDecode component success");
    } else {
        return;
    }
    if (auto render = videoRender_.load()) {
        render->notifyVideoInfo(demuxer_->videoInfo());
    }
}

void Player::Impl::initPlayerInfo() noexcept {
    info_.isValid = true;
    info_.hasAudio = demuxer_->hasAudio();
    info_.hasVideo = demuxer_->hasVideo();
    info_.duration = demuxer_->totalDuration().second();
    PlayerSetting setting;
    params_.withReadLock([&setting](auto& p){
        setting = p->setting;
    });
    if (info_.hasAudio) {
        LogI("has audio, audio info:{}", demuxer_->audioInfo()->mediaInfo);
        createAudioComponent(setting);
    } else {
        LogI("no audio");
    }
    
    if (info_.hasVideo) {
        LogI("has audio, video info:{}", demuxer_->videoInfo()->mediaInfo);
        createVideoComponent(setting);
    } else {
        LogI("no video");
    }
    
    doPause();
    if (info_.hasVideo) {
        dataProvider_->start(); //render first frame
        ownerThread_->start();
        stats_.isForceVideoRendered = true;
    }
    LogI("init player success.");
    setState(PlayerState::Ready);
}

void Player::Impl::handleAudioPacket(AVFramePtrArray& audioPackets) noexcept {
    for (auto& packet : audioPackets) {
        auto pts = packet->ptsTime();
        stats_.audioDemuxedTime = pts + packet->duration / 1000.0;
        if (demuxer_->isCompleted()) {
            stats_.audioDemuxedTime = info_.duration;
        }
        if (seekRequest_.has_value() && pts < seekRequest_.value().seekTime) {
            LogI("discard audio frame:{}", packet->pts);
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
        if (seekRequest_.has_value() && pts < seekRequest_.value().seekTime) {
            if (!demuxer_->isCompleted()) {
                packet->isDiscard = true;
                LogI("[seek info]discard video frame:{}, time:{}", pts, seekRequest_.value().seekTime);
            }
        }
        LogI("demux video frame:{}, pts:{}, dts:{}, seek:{}", packet->index, pts, packet->dtsTime(), seekRequest_.has_value() ? seekRequest_.value().seekTime : 0);
        videoPackets_.push_back(std::move(packet));
    }
}

void Player::Impl::demuxData() noexcept {
    if (demuxer_ && demuxer_->isCompleted()) {
        return;
    }
    std::vector<DataPacket> dataList;
    dataList_.withLock([&](auto& list) {
        list.swap(dataList);
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
            initPlayerInfo();
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

void Player::Impl::pushAudioFrameDecode() noexcept {
    if (!audioDecodeComponent_ || !audioRender_) {
        return;
    }
    if (audioPackets_.empty()) {
        if (demuxer_ && demuxer_->isCompleted()) {
            audioDecodeComponent_->pause();
        }
        return;
    }
    auto pendingFrameDtsTime = audioPackets_.front()->dtsTime();
    auto audioClockTime = audioRenderTime();
    bool isNeedPushFrame = false;
    auto delta = (stats_.audioDecodeDelta + stats_.audioRenderDelta).second() + static_cast<double>(audioPackets_.front()->duration) / 1000.0;
    if (!stats_.isFirstAudioRendered || audioClockTime >= (pendingFrameDtsTime - delta)) {
        isNeedPushFrame = true;
    }
    if (!isNeedPushFrame) {
        return;
    }
    LogI("push audio frame pushFrameDecode:{} dts: {} delta:{}, audioClockTime:{}", audioPackets_.front()->index, audioPackets_.front()->dtsTime(), delta, audioClockTime);
    audioPackets_.front()->stats.prepareDecodeStamp = Time::nowTimeStamp();
    audioDecodeComponent_->send(std::move(audioPackets_.front()));
    audioPackets_.pop_front();
}

void Player::Impl::pushVideoFrameDecode(bool ) noexcept {
    if (!videoDecodeComponent_) {
        return;
    }
    ///TODO: FIX DTS
    if (videoDecodeComponent_) {
        if (!videoPackets_.empty()) {
            LogI("push video frame pushFrameDecode:{}", videoPackets_.front()->index);
            videoDecodeComponent_->send(std::move(videoPackets_.front()));
            videoPackets_.pop_front();
        }
    }
}

void Player::Impl::pushAVFrameDecode() noexcept {
    if (info_.hasAudio) {
        pushAudioFrameDecode();
    }
    if (info_.hasVideo) {
        pushVideoFrameDecode();
    }
}

void Player::Impl::pushVideoFrameToRender() noexcept {
    if (!stats_.isForceVideoRendered) {
        long double diff = 0.0;
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
            framePtr = std::move(frames.front());
            frames.pop_front();
        }
    });
    if (!framePtr) {
        return;
    }
    render->pushVideoFrameRender(framePtr);
    auto renderTime = framePtr->ptsTime();
    auto nowState = state();
    LogI("push video frame render time:{}, is force:{}, state:{}",
         renderTime, stats_.isForceVideoRendered, static_cast<int>(nowState));
    render->clock().setTime( Time::TimePoint(renderTime));
    if (!stats_.isForceVideoRendered) {
        return;
    }
    if (seekRequest_.has_value()) {
        if (seekRequest_.value().isAccurate && isSeekingWhilePlaying_) {
            setState(PlayerState::Playing);
            isSeekingWhilePlaying_ = false;
        } else {
            setState(PlayerState::Ready);
        }
        seekRequest_.reset();
    }
    stats_.isForceVideoRendered = false;
}

void Player::Impl::pushAVFrameToRender() noexcept {
    audioFrames_.withLock([this](auto& audioFrames){
        if (!info_.hasAudio || !audioRender_) {
            return;
        }
        if (audioFrames.empty()) {
            return;
        }
        auto& frame = audioFrames.front();
        if (audioRender_->send(frame)) {
            LogI("push audio:{}", frame->ptsTime());
            audioFrames.pop_front();
            if (stats_.audioRenderDelta == 0) {
                stats_.audioRenderDelta = Time::nowTimeStamp();
            }
        }
    });
    pushVideoFrameToRender();
}

void Player::Impl::process() noexcept {
    handleEvent(receiver_->tryReceiveAll());
    auto nowState = state();
    if (nowState == PlayerState::Stop) {
        return;
    }
    demuxData();
    pushAVFrameDecode();
    if (seekRequest_.has_value() || nowState == PlayerState::Buffering) {
        pushVideoFrameToRender();
    } else if (nowState == PlayerState::Playing) {
        pushAVFrameToRender();
    }
}

void Player::Impl::doPlay() noexcept {
    LogI("do play");
    ownerThread_->start();
    if (audioRender_) {
        audioRender_->play();
    }
    if (audioDecodeComponent_) {
        audioDecodeComponent_->start();
    }
    if (auto render = videoRender_.load()) {
        render->start();
    }
}

void Player::Impl::doPause() noexcept {
    LogI("do pause");
    if (audioDecodeComponent_) {
        audioDecodeComponent_->pause();
    }
    if (videoDecodeComponent_) {
        videoDecodeComponent_->pause();
    }
    if (audioRender_) {
        audioRender_->pause();
    }
    if (auto render = videoRender_.load()) {
        render->pause();
    }
    isSeekingWhilePlaying_ = false;
}

void Player::Impl::doStop() noexcept {
    LogI("do stop start");
    if (dataProvider_) {
        dataProvider_->close();
        dataProvider_.reset();
    }
    audioRender_->stop();
    audioRender_.reset();
    demuxer_.reset();
    audioDecodeComponent_.reset();
    videoDecodeComponent_.reset();
    if (ownerThread_) {
        ownerThread_->stop();
    }
    if (auto render = videoRender_.load()) {
        render->pause();
    }
    GLContextManager::shareInstance().removeMainContext(playerId_);
    isReleased_ = true;
    cond_.notify_all();
    LogI("do stop end");
}

void Player::Impl::doSeek(PlayerSeekRequest seekRequest) noexcept {
    if (!demuxer_) {
        return;
    }
    
    constexpr long double kSeekThreshold = 0.1;
    auto seekTime = seekRequest.seekTime;
    auto playedTime = currentPlayedTime();
    if (!seekRequest.isAccurate && (isEqual(playedTime, seekTime, kSeekThreshold) || seekRequest_.has_value())) {
        return;
    }

    auto nowState = state();
    if (nowState == PlayerState::Playing) {
        setState(PlayerState::Pause);
        isSeekingWhilePlaying_ = true;
    } else if (nowState == PlayerState::Completed) {
        setState(PlayerState::Pause);
        isSeekingWhilePlaying_ = false;
    }
    setState(PlayerState::Buffering);
    seekRequest_ = seekRequest;
    auto demuxedTime = demuxedDuration();
    auto videoInfo = demuxer_->videoInfo();
    LogI("[seek info]seek to time:{}, playedTime:{}, demuxedTime:{}", seekTime, playedTime, demuxedTime);
    if (playedTime <= seekTime && seekTime <= demuxedTime) {
        if (info_.hasVideo) {
            videoFrames_.withLock([seekTime](auto& queue){
                while(!queue.empty() && queue.front()->ptsTime() < seekTime) {
                    queue.pop_front();
                }
            });
            auto it = videoPackets_.begin();
            while(it != videoPackets_.end() && (*it)->ptsTime() < seekTime) {
                (*it)->isDiscard = true;
                it++;
            }
        }
        
        if (info_.hasAudio) {
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
        audioDecodeComponent_->flush();
        videoDecodeComponent_->flush();
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

void Player::Impl::doLoop() noexcept {
    if (!demuxer_) {
        return;
    }
    auto seekPos = demuxer_->getSeekToPos(0);
    dataProvider_->seek(seekPos);
    dataProvider_->pause();
    demuxer_->seekPos(seekPos);
    if (audioRender_) {
        audioRender_->seek(0);
    }
    setVideoRenderTime(0);
    seekRequest_.reset();
    isSeekingWhilePlaying_ = false;
    stats_.reset();
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
    auto nowTime = currentPlayedTime();
    auto currentState = state();
    if (currentState == PlayerState::Playing && dataProvider_->isCompleted() && isgreaterequal(nowTime, info_.duration)) {
        notifyPlayedTime(); //notify time to end
        LogI("play end.");
        bool isLoop = false;
        params_.withReadLock([&isLoop](auto& p){
            isLoop = p->setting.isLoop;
        });
        doLoop();
        if (!isLoop){
            changeState = PlayerState::Completed;
        } else {
            dataProvider_->start();
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
    long double time = 0.0;
    if (info_.hasAudio && info_.hasVideo) {
        auto videoTime = videoRenderTime();
        auto audioTime = audioRenderTime();
        time = std::min(videoTime, audioTime);
        LogI("notifyTime:{}, video time:{}, audio time:{}", time, videoTime, audioTime);
    } else if (info_.hasAudio) {
        time = audioRenderTime();
        LogI("notifyTime:{} (audio)", time);
    } else if (info_.hasVideo) {
        time = videoRenderTime();
        LogI("notifyTime:{} (video)", time);
    }
    observer_.withReadLock([this, time](auto& observer){
        if (auto ptr = observer.lock(); ptr) {
            ptr->notifyPlayedTime(playerId_, time);
        }
    });
}

void Player::Impl::notifyCacheTime() noexcept {
    long double time = demuxedDuration();
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

long double Player::Impl::currentPlayedTime() noexcept {
    if (info_.hasAudio && info_.hasVideo) {
        return std::min(audioRenderTime(), videoRenderTime());
    } else if (info_.hasAudio) {
        return audioRenderTime();
    } else if (info_.hasVideo) {
        return videoRenderTime();
    }
    return 0;
}

long double Player::Impl::demuxedDuration() const noexcept {
    if (info_.hasAudio && info_.hasVideo) {
        return std::min(stats_.audioDemuxedTime, stats_.videoDemuxedTime);
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
    cacheTime = std::max(0.0l, cacheTime);
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
        (info_.isValid && isEqualOrGreater(playedTime, info_.duration, 0.1l)) ) {
        pauseOwnerThread(nowState);
        startRead(false);
    } else if (dataProvider_->isCompleted()) {
        startRead(false);
    } else if (!dataProvider_->isRunning()) {
        pauseOwnerThread(nowState);
    }
}

long double Player::Impl::videoRenderTime() noexcept {
    long double videoTime = 0.0;
    if (auto render = videoRender_.load()) {
        videoTime = render->clock().time().second();
    }
    return videoTime;
}

long double Player::Impl::audioRenderTime() noexcept {
    if (audioRender_) {
        return audioRender_->playedTime().second();
    }
    return 0;
}

AVFrameRefPtr Player::Impl::requestRender() noexcept {
    long double diff = 0;
    LogI("requestRender: {}, {}", audioRenderTime(), videoRenderTime());
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
            framePtr = std::move(frames.front());
            frames.pop_front();
        }
    });
    if (framePtr) {
        auto pts= framePtr->ptsTime();
        LogI("push video frame render:{}, pts:{}", pts, framePtr->pts);
        setVideoRenderTime(pts);
        stats_.isForceVideoRendered = false;
    }
    return framePtr;
}

void Player::Impl::setRenderImpl(std::weak_ptr<IVideoRender> render) noexcept {
    videoRender_.store(render);
    auto ptr = videoRender_.load();
    if (ptr) {
        ptr->setRequestRenderFunc([this]() -> AVFrameRefPtr{
            return requestRender();
        });
    }
}

void Player::Impl::setVideoRenderTime(long double time) noexcept {
    if (auto render = videoRender_.load()) {
        render->clock().setTime(Time::TimePoint(time));
    }
}

}//end namespace slark
