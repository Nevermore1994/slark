//
//  PlayerImpl.cpp
//  slark
//
//  Created by Nevermore on 2022/5/4.
//

#include "Player.h"
#include "Log.hpp"
#include "PlayerImpl.h"
#include "Util.hpp"
#include "Base.h"
#include "AudioRenderComponent.h"
#include "DecoderComponent.h"
#include "Event.h"
#include "IDemuxer.h"
#include "Mp4Demuxer.hpp"
#include "DecoderConfig.h"
#include "GLContextManager.h"

namespace slark {

const long double kAVSyncThreshold = 0.1; //100ms

Player::Impl::Impl(std::unique_ptr<PlayerParams> params)
    : playerId_(Random::uuid()) {
    GLContextManager::shareInstance().addMainContext(playerId_, params->mainGLContext);
    params_.withWriteLock([&params](auto& p){
        p = std::move(params);
    });
    init();
}

Player::Impl::~Impl() {
    updateState(PlayerState::Stop);
    isStoped_ = true;
    std::unique_lock<std::mutex> lock(releaseMutex_);
    cond_.wait(lock, [this]{
        return state() == PlayerState::Stop;
    });
}

void Player::Impl::seek(long double time, bool isAccurate) noexcept {
    auto ptr = buildEvent(EventType::Seek);
    PlayerSeekRequest req;
    req.seekTime = time;
    req.isAccurate = isAccurate;
    ptr->data = req;
    sender_->send(std::move(ptr));
}

void Player::Impl::updateState(PlayerState state) noexcept {
    sender_->send(buildEvent(state));
    ownerThread_->resume();
}

void Player::Impl::addObserver(IPlayerObserverPtr observer) noexcept {
    observer_.withWriteLock([observer = std::move(observer)](auto& ob) {
        ob = std::move(observer);
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

    if (!setupIOHandler()) {
        LogE("setupIOHandler failed.");
        return;
    }
    ownerThread_ = std::make_unique<Thread>("playerThread", &Player::Impl::process, this);
    ownerThread_->runLoop(200ms, [this](){
        if (state() == PlayerState::Playing) {
            notifyTime(); 
        }
    });
    
    readHandler_->start();
    ownerThread_->start();
    setState(PlayerState::Buffering);
    LogI("player buffering start.");
}

bool Player::Impl::setupIOHandler() noexcept {
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

    ReaderSetting setting;
    setting.callBack = [this](IOData data, IOState state) {
        LogI("receive data offset:{} size: {}", data.offset, data.length());
        if (data.data) {
            dataList_.withLock([&](auto& dataList) {
                dataList.emplace_back(std::move(data));
            });
        }

        if (state == IOState::Error) {
            sender_->send(buildEvent(EventType::ReadError));
        } else if (state == IOState::EndOfFile) {
            isReadCompleted_ = true;
        }
    };
    readHandler_ = std::make_unique<Reader>();
    if (!readHandler_->open(path, std::move(setting))) {
        setState(PlayerState::Error);
        notifyEvent(PlayerEvent::OnError, std::to_string(static_cast<uint8_t>(PlayerErrorCode::OpenFileError)));
        return false;
    }
    probeBuffer_ = std::make_unique<Buffer>(readHandler_->size());
    return true;
}

bool Player::Impl::createDemuxer(IOData& data) noexcept {
    if (!probeBuffer_->append(static_cast<uint64_t>(data.offset), std::move(data.data))) {
        return false;
    }
    auto dataView = probeBuffer_->view();
    auto demuxType = DemuxerManager::shareInstance().probeDemuxType(dataView);
    if (demuxType == DemuxerType::Unknown) {
        return false;
    }

    demuxer_ = DemuxerManager::shareInstance().create(demuxType);
    if (!demuxer_) {
        LogE("not found demuer type:{}", static_cast<int32_t>(demuxType));
        return false;
    }
    return true;
}

bool Player::Impl::openDemuxer(IOData& data) noexcept {
    if (!probeBuffer_->append(data.offset, std::move(data.data))) {
        return false;
    }
    if (demuxer_ == nullptr) {
        return false;
    }
    auto res = false;
    if (res = demuxer_->open(probeBuffer_); res) {
        probeBuffer_.reset();
        LogI("init demuxer success.");
        initPlayerInfo();
    }
    if (demuxer_->type() == DemuxerType::MP4) {
        auto mp4Demuxer = dynamic_cast<Mp4Demuxer*>(demuxer_.get());
        if (res) {
            auto dataStart = demuxer_->headerInfo()->headerLength + 8; //skip size and type
            ReadRange range;
            range.readPos = dataStart;
            range.readSize = demuxer_->headerInfo()->dataSize;
            demuxer_->seekPos(dataStart);
            readHandler_->setReadRange(range);
//#if DEBUG
//            auto ss = mp4Demuxer->description();
//            LogI("mp4 info:{}", ss);
//#endif
        } else {
            int64_t moovBoxStart = 0;
            uint32_t moovBoxSize = 0;
            constexpr int64_t kMb = 1024 * 1024;
            if (!mp4Demuxer->probeMoovBox(*probeBuffer_, moovBoxStart, moovBoxSize)) {
                auto pos = static_cast<int64_t>(probeBuffer_->pos());
                if (pos == 0) {
                    auto headerInfo = mp4Demuxer->headerInfo();
                    if (headerInfo) {
                        pos = headerInfo->headerLength + headerInfo->dataSize;
                    } else {
                        pos = probeBuffer_->totalSize() - kMb;
                    }
                } else {
                    pos -= kMb;
                }
                auto probePos = pos;
                if (probePos < 0) {
                    return false;
                }
                demuxer_->seekPos(probePos);
                readHandler_->seek(probePos);
                probeBuffer_->reset();
                probeBuffer_->setOffset(probePos);
            }
        }
    }
    return res;
}

void Player::Impl::initPlayerInfo() noexcept {
    info_.hasAudio = demuxer_->hasAudio();
    info_.hasVideo = demuxer_->hasVideo();
    info_.duration = demuxer_->totalDuration().second();
    PlayerSetting setting;
    params_.withReadLock([&setting](auto& p){
        setting = p->setting;
    });
    if (info_.hasAudio) {
        LogI("has audio, audio info:{}", demuxer_->audioInfo()->mediaInfo);
        do {
            auto decodeType = DecoderManager::shareInstance().getDecoderType(demuxer_->audioInfo()->mediaInfo, setting.enableAudioSoftDecode);
            if (!DecoderManager::shareInstance().contains(decodeType)) {
                LogE("not found decoder:media info {}", demuxer_->audioInfo()->mediaInfo);
                break;
            }
            audioDecodeComponent_ = std::make_unique<DecoderComponent>([this](auto frame) {
                if (!statistics_.isFirstAudioRendered) {                    statistics_.audioDecodeDelta = frame->stats.decodedStamp - frame->stats.prepareDecodeStamp;
                }
                audioFrames_.withWriteLock([&frame](auto& frames){
                    frames.push_back(std::move(frame));
                });
            });
            if (!audioDecodeComponent_) {
                LogI("create audio decode Component error:{}", demuxer_->audioInfo()->mediaInfo);
                break;
            }
            auto config = std::make_shared<AudioDecoderConfig>();
            config->bitsPerSample = demuxer_->audioInfo()->bitsPerSample;
            config->channels = demuxer_->audioInfo()->channels;
            config->sampleRate = demuxer_->audioInfo()->sampleRate;
            config->timeScale = demuxer_->audioInfo()->timeScale;
            if (!audioDecodeComponent_->open(decodeType, config)) {
                LogI("create audio decode component successs");
                break;
            }
            audioRender_ = std::make_unique<AudioRenderComponent>(demuxer_->audioInfo());
            audioRender_->firstFrameRenderCallBack = [this](Time::TimePoint timestamp){
                statistics_.isFirstAudioRendered = true;
                statistics_.audioRenderDelta = timestamp - statistics_.audioRenderDelta;
            };
            LogI("create audio render successs");
        } while(false);
    } else {
        LogI("no audio");
    }
    
    if (info_.hasVideo) {
        LogI("has audio, video info:{}", demuxer_->videoInfo()->mediaInfo);
        do {
            auto decodeType = DecoderManager::shareInstance().getDecoderType(demuxer_->videoInfo()->mediaInfo, setting.enableVideoSoftDecode);
            if (!DecoderManager::shareInstance().contains(decodeType)) {
                LogE("not found decoder:media info {}", demuxer_->videoInfo()->mediaInfo);
                break;
            }
            videoDecodeComponent_ = std::make_unique<DecoderComponent>([this](auto frame) {
                videoFrames_.withWriteLock([&frame](auto& frames){
                    frames.push_back(std::move(frame));
                });
            });
            if (!videoDecodeComponent_) {
                LogI("create video decoder error:{}", demuxer_->videoInfo()->mediaInfo);
                break;
            }
            auto config = std::make_shared<VideoDecoderConfig>();
            config->width = demuxer_->videoInfo()->width;
            config->height = demuxer_->videoInfo()->height;
            config->naluHeaderLength = demuxer_->videoInfo()->naluHeaderLength;
            config->sps = demuxer_->videoInfo()->sps;
            config->pps = demuxer_->videoInfo()->pps;
            if (videoDecodeComponent_->open(decodeType, config)) {
                LogI("create video decode component success");
            } else {
                break;
            }
            videoRender_.withReadLock([this](auto& p){
                if (auto ptr = p.lock(); ptr) {
                    ptr->notifyVideoInfo(demuxer_->videoInfo());
                }
            });
        } while(false);
    } else {
        LogI("no video");
    }
    
    doPause();
    if (info_.hasVideo) {
        readHandler_->resume(); //render first frame
        ownerThread_->resume();
    }
    setState(PlayerState::Ready);
    LogE("init player success.");
}

void Player::Impl::demuxData() noexcept {
    if (demuxer_ && demuxer_->isCompleted()) {
        return;
    }
    std::vector<IOData> dataList;
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

        if (!demuxer_->isInited() && !openDemuxer(data)) {
            continue;
        }
        auto parseResult = demuxer_->parseData(std::move(data.data), data.offset);
        //LogI("demux frame count %d, state: %d", frameList.size(), code);
        if (parseResult.resultCode == DemuxerResultCode::Failed && dataList.empty()) {
            break;
        }
        if (!parseResult.audioFrames.empty()) {
            for (auto& packet : parseResult.audioFrames) {
                LogI("demux audio frame:{}, pts:{}, dts:{}, offset:{}", packet->index, packet->dts, packet->pts, packet->offset);
                audioPackets_.push_back(std::move(packet));
            }
        }
        if (!parseResult.videoFrames.empty()) {
            for (auto& packet : parseResult.videoFrames) {
                LogI("demux video frame:{}, pts:{}, dts:{}, offset:{}", packet->index, packet->dts, packet->pts, packet->offset);
                videoPackets_.push_back(std::move(packet));
            }
        }
        if (isStoped_) {
            break;
        }
    }
}

void Player::Impl::pushAudioFrameDecode() noexcept {
    if (!audioDecodeComponent_ || !audioRender_) {
        return;
    }
    if (audioPackets_.empty()) {
        return;
    }
    auto pendingFrameDtsTime = audioPackets_.front()->dtsTime();
    auto audioClockTime = audioRender_->clock().time().second();
    bool isNeedPushFrame = false;
    auto delta = (statistics_.audioDecodeDelta + statistics_.audioRenderDelta).second() + static_cast<double>(audioPackets_.front()->duration) / 1000.0;
    if (!statistics_.isFirstAudioRendered) {
        isNeedPushFrame = true;
    } else if (audioClockTime >= (pendingFrameDtsTime - delta)) {
        isNeedPushFrame = true;
    }
    if (!isNeedPushFrame) {
        return;
    }
    LogI("push audio frame decode:{} dts: {} delta:{}, audioClockTime:{}", audioPackets_.front()->index, audioPackets_.front()->dtsTime(), delta, audioClockTime);
    audioPackets_.front()->stats.prepareDecodeStamp = Time::nowTimeStamp();
    audioDecodeComponent_->send(std::move(audioPackets_.front()));
    audioPackets_.pop_front();
}

void Player::Impl::pushVideoFrameDecode() noexcept {
    if (!videoDecodeComponent_) {
        return;
    }
    ///TODO: FIX DTS
    if (videoDecodeComponent_ && videoDecodeComponent_->isNeedPushFrame()) {
        if (!videoPackets_.empty()) {
            LogI("push video frame decode:{}", videoPackets_.front()->index);
            videoDecodeComponent_->send(std::move(videoPackets_.front()));
            videoPackets_.pop_front();
        }
    }
}

void Player::Impl::pushFrameDecode() noexcept {
    if (info_.hasAudio) {
        pushAudioFrameDecode();
    }
    if (info_.hasVideo) {
        pushVideoFrameDecode();
    }
}

void Player::Impl::pushAVFrameToRender() noexcept {
    audioFrames_.withLock([this](auto& audioFrames){
        if (!info_.hasAudio || !audioRender_ || audioRender_->isFull()) {
            return;
        }
        if (audioFrames.empty()) {
            return;
        }
        LogI("[seek info] push audio:{}", audioFrames.front()->ptsTime());
        audioRender_->send(std::move(audioFrames.front()));
        audioFrames.pop_front();
        if (statistics_.audioRenderDelta == 0) {
            statistics_.audioRenderDelta = Time::nowTimeStamp();
        }
    });
    videoRender_.withReadLock([this](auto& p){
        if (statistics_.isFirstVideoRendered) {
            long double diff = 0.0;
            if (info_.hasAudio && info_.hasVideo) {
                diff = audioRenderTime() - videoRenderTime();
            } else if(!info_.hasVideo) {
                return;
            }
            if (diff < kAVSyncThreshold) {
                return;
            }
        }
        if (auto ptr = p.lock(); ptr) {
            AVFramePtr framePtr = nullptr;
            videoFrames_.withLock([&framePtr](auto& frames){
                if (!frames.empty()) {
                    framePtr = std::move(frames.front());
                    frames.pop_front();
                }
            });
            if (framePtr) {
                ptr->pushVideoFrameRender(framePtr->opaque);
                auto t = framePtr->ptsTime();
                LogI("[seek info] push vieo frame render:{}, pts:{}", t, framePtr->pts);
                ptr->clock().setTime(static_cast<uint64_t>(framePtr->ptsTime() * Time::kMicroSecondScale));
                if (!statistics_.isFirstVideoRendered) {
                    doPause();
                    LogI("fisrt frame rended.");
                }
                statistics_.isFirstVideoRendered = true;
            }
        }
    });
}

void Player::Impl::process() noexcept {
    handleEvent(receiver_->tryReceiveAll());
    auto nowState = state();
    if (nowState == PlayerState::Stop) {
        return;
    }
    if (nowState == PlayerState::Playing) {
        pushAVFrameToRender();
        pushFrameDecode();
        demuxData();
    } else if (nowState == PlayerState::Buffering) {
        demuxData();
        pushFrameDecode();
    } else if (nowState == PlayerState::Ready) {
        demuxData();
        pushVideoFrameDecode();
        pushAVFrameToRender();
    }
        
    checkCacheState();
}

void Player::Impl::doPlay() noexcept {
    ownerThread_->resume();
    readHandler_->resume();
    if (audioRender_) {
        audioRender_->play();
    }
    if (audioDecodeComponent_) {
        audioDecodeComponent_->resume();
    }
    videoRender_.withReadLock([this](auto& p){
        if (auto ptr = p.lock(); ptr) {
            ptr->start();
        }
    });
}

void Player::Impl::doPause() noexcept {
    ownerThread_->pause();
    readHandler_->pause();
    if (audioDecodeComponent_) {
        audioDecodeComponent_->pause();
    }
    if (audioRender_) {
        audioRender_->pause();
    }
    videoRender_.withReadLock([this](auto& p){
        if (auto ptr = p.lock(); ptr) {
            ptr->pause();
        }
    });
}

void Player::Impl::doStop() noexcept {
    if (readHandler_) {
        readHandler_->stop();
        readHandler_.reset();
    }
    audioRender_.reset();
    demuxer_.reset();
    audioDecodeComponent_.reset();
    if (ownerThread_) {
        ownerThread_->stop();
    }
    videoRender_.withReadLock([this](auto& p){
        if (auto ptr = p.lock(); ptr) {
            ptr->pause();
        }
    });
    GLContextManager::shareInstance().removeMainContext(playerId_);
    cond_.notify_all();
}

void Player::Impl::doSeek() noexcept {
    if (!demuxer_) {
        return;
    }
    doPause();
    isReadCompleted_ = false;
    auto seekPos = demuxer_->getSeekToPos(seekRequest_.value().seekTime);
    audioDecodeComponent_->flush();
    videoDecodeComponent_->flush();
    audioPackets_.clear();
    videoPackets_.clear();
    audioFrames_.withWriteLock([](auto& frames) {
        frames.clear();
    });
    videoFrames_.withWriteLock([](auto& frames) {
        frames.clear();
    });
    readHandler_->seek(seekPos);
    demuxer_->seekPos(seekPos);
    uint64_t time = static_cast<uint64_t>(seekRequest_.value().seekTime * Time::kMicroSecondScale);
    audioRender_->seek(time);
    videoRender_.withReadLock([time](auto& p){
        if (auto ptr = p.lock(); ptr) {
            ptr->clock().setTime(time);
        }
    });
    LogI("do seek pos:{}, time:{}", seekPos, seekRequest_.value().seekTime);
    seekRequest_.reset();
    setState(PlayerState::Ready);
}

void Player::Impl::doLoop() noexcept {
    if (!demuxer_) {
        return;
    }
    isReadCompleted_ = false;
    auto seekPos = demuxer_->getSeekToPos(0);
    readHandler_->seek(seekPos);
    readHandler_->pause();
    demuxer_->seekPos(seekPos);
    if (audioRender_) {
        audioRender_->seek(0);
    }
    statistics_.reset();
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
    notifyState(state);
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
                audioRender_->setVolume(volume / 100.f);
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
            seekRequest_ = std::any_cast<PlayerSeekRequest>(event->data);
            doSeek();
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
    if (currentState == PlayerState::Playing && isReadCompleted_ && isgreaterequal(nowTime, info_.duration)) {
        notifyTime(); //notify time to end
        LogI("play end.");
        bool isLoop;
        params_.withReadLock([&isLoop](auto& p){
            isLoop = p->setting.isLoop;
        });
        doLoop();
        if (!isLoop){
            changeState = PlayerState::Completed;
        } else {
            readHandler_->resume();
        }
    }
    if (changeState != PlayerState::Unknown) {
        setState(changeState);
    }
}

void Player::Impl::notifyEvent(PlayerEvent event, std::string value) noexcept {
    observer_.withReadLock([this, event, value = std::move(value)](auto& observer){
        if (auto ptr = observer.lock(); ptr) {
            ptr->notifyEvent(playerId_, event, value);
        }
    });
}

void Player::Impl::notifyState(PlayerState state) noexcept {
    observer_.withReadLock([this, state](auto& observer){
        if (auto ptr = observer.lock(); ptr) {
            ptr->notifyState(playerId_, state);
        }
    });
}

void Player::Impl::notifyTime() noexcept {
    auto videoTime = videoRenderTime();
    auto audioTime = audioRenderTime();
    double time = std::min(videoTime, audioTime);
    observer_.withReadLock([this, time](auto& observer){
        if (auto ptr = observer.lock(); ptr) {
            ptr->notifyTime(playerId_, time);
        }
    });
    LogI("notifyTime:{}, video time:{}, audio time:{}", time, videoTime, audioTime);
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
    return std::min(audioRenderTime(), videoRenderTime());
}

uint32_t Player::Impl::demuxedDuration() noexcept {
    uint32_t audioDemuxedDuration = 0;
    uint32_t videoDemuxedDuration = 0;
    if (info_.hasAudio && !audioPackets_.empty()) {
        audioDemuxedDuration = audioPackets_.front()->duration * audioPackets_.size();
    }
    
    if (info_.hasVideo && !videoPackets_.empty()) {
        videoDemuxedDuration = videoPackets_.front()->duration * videoPackets_.size();
    }
    return std::min(audioDemuxedDuration, videoDemuxedDuration);
}

void Player::Impl::checkCacheState() noexcept {
    if (state() != PlayerState::Playing) {
        return;
    }
    PlayerSetting setting;
    params_.withReadLock([&setting](auto& p){
        setting = p->setting;
    });
    auto cacheTime = demuxedDuration();
    if (cacheTime < setting.minCacheTime && !isReadCompleted_) {
        readHandler_->resume();
    } else if (cacheTime >= setting.maxCacheTime) {
        readHandler_->pause();
    }
}

long double Player::Impl::videoRenderTime() noexcept {
    long double videoTime = 0.0;
    videoRender_.withReadLock([&videoTime](auto& p){
        if (auto ptr = p.lock(); ptr) {
            videoTime = ptr->clock().time().second();
        }
    });
    return videoTime;
}

long double Player::Impl::audioRenderTime() noexcept {
    if (audioRender_) {
        return audioRender_->playedTime().second();
    }
    return 0;
}

void* Player::Impl::requestRender() noexcept {
    long double diff = 0;
    LogI("[seek info] requestRender: {}, {}", audioRenderTime(), videoRenderTime());
    if (info_.hasAudio && info_.hasVideo) {
        diff = audioRenderTime() - videoRenderTime();
    } else if(!info_.hasVideo) {
        return nullptr;
    }
    
    if (diff < (-kAVSyncThreshold) && statistics_.isFirstVideoRendered) {
        auto videoInfo = demuxer_->videoInfo();
        auto videoTime = videoRenderTime() - demuxer_->videoInfo()->frameDuration();
        
        videoTime = videoTime < 0 ? 0 : videoTime;
        videoRender_.withReadLock([videoTime](auto& p){
            if (auto ptr = p.lock(); ptr) {
                ptr->clock().setTime(static_cast<uint64_t>(videoTime * Time::kMicroSecondScale));
            }
        });
        return nullptr;
    }
    AVFramePtr framePtr = nullptr;
    videoFrames_.withWriteLock([&framePtr](auto& frames){
        if (!frames.empty()) {
            framePtr = std::move(frames.front());
            frames.pop_front();
        }
    });
    if (framePtr) {
        videoRender_.withReadLock([&framePtr](auto& p){
            if (auto ptr = p.lock(); ptr) {
                auto t = framePtr->ptsTime();
                LogI("[seek info] push vieo frame render:{}, pts:{}", t, framePtr->pts);
                ptr->clock().setTime(static_cast<uint64_t>(framePtr->ptsTime() * Time::kMicroSecondScale));
            }
        });
        statistics_.isFirstVideoRendered = true;
    }
    return framePtr ? framePtr->opaque : nullptr;
}

void Player::Impl::setRenderImpl(std::weak_ptr<IVideoRender>& render) noexcept {
    videoRender_.withWriteLock([render](auto& videoRender){
        videoRender = render;
    });
}

}//end namespace slark
