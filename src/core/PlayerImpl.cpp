//
//  PlayerImpl.cpp
//  slark
//
//  Created by Nevermore on 2022/5/4.
//

#include "AudioRenderComponent.h"
#include "DecoderComponent.h"
#include "Event.h"
#include "IDemuxer.h"
#include "Mp4Demuxer.hpp"
#include "Player.h"
#include "Log.hpp"
#include "PlayerImpl.h"
#include "Util.hpp"
#include "Base.h"

namespace slark {

Player::Impl::Impl(std::unique_ptr<PlayerParams> params)
    : playerId_(Random::uuid()) {
    params_.withWriteLock([&params](auto& p){
        p = std::move(params);
    });
    init();
}

Player::Impl::~Impl() {
    doStop();
}

void Player::Impl::seek(long double time, bool isAccurate) noexcept {
    auto ptr = buildEvent(EventType::Seek);
    PlayerSeekRequest req;
    req.seekTime = static_cast<uint64_t>(time * 1000000);
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

    ownerThread_ = std::make_unique<Thread>("playerThread", &Player::Impl::process, this);
    ownerThread_->runLoop(200ms, [this](){
        if (state() == PlayerState::Playing) {
            notifyTime(); 
        }
    });
    ///set file path
    std::string path;
    params_.withReadLock([&](auto& params){
        path = params->item.path;
    });
    if (path.empty()) {
        LogE("error, player param invalid.");
        setState(PlayerState::Stop);
        return;
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
    } else {
        setState(PlayerState::Buffering);
        readHandler_->start();
    }
    probeBuffer_ = std::make_unique<Buffer>(readHandler_->size());
    ownerThread_->start();
    LogI("player initializing start.");
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
            auto dataStart = demuxer_->headerInfo()->headerLength + 8; // //skip size and type
            demuxer_->seekPos(dataStart);
            readHandler_->seek(dataStart);
#if DEBUG
            auto ss = mp4Demuxer->description();
            LogI("mp4 info:{}", ss);
#endif
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
    if (info_.hasAudio) {
        LogI("has audio, audio info:{}", demuxer_->audioInfo()->mediaInfo);
        do {
            audioDecoder_ = createAudioDecoderComponent(demuxer_->audioInfo()->mediaInfo);
            if (!audioDecoder_) {
                LogI("create decoder error:{}", demuxer_->audioInfo()->mediaInfo);
                break;
            } else {
                LogI("create decoder successs");
            }
            audioRender_ = std::make_unique<Audio::AudioRenderComponent>(demuxer_->audioInfo());
            LogI("create audio render successs");
        } while(false);
    } else {
        LogI("no audio");
    }
    ownerThread_->pause();
    readHandler_->pause();
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
    }
}

std::unique_ptr<DecoderComponent> Player::Impl::createAudioDecoderComponent(std::string_view mediaInfo) noexcept {
    PlayerSetting setting;
    params_.withReadLock([&setting](auto& p){
        setting = p->setting;
    });
    auto decodeType = DecoderManager::shareInstance().getDecoderType(mediaInfo, setting.enableAudioSoftDecode);
    if (!DecoderManager::shareInstance().contains(decodeType)) {
        LogE("not found decoder:media info {}  type:{}", mediaInfo, static_cast<int32_t>(decodeType));
        return nullptr;
    }
    auto decoder = std::make_unique<DecoderComponent>(decodeType, [this](auto frameArray) {
        //LogI("receive frame size: {}", frameArray.size());
        audioFrames_.withLock([&frameArray](auto& frames) {
            std::ranges::for_each(frameArray, [&frames] (auto& frame) { frames.push_back(std::move(frame)); });
        });
    });
    return decoder;
}

std::unique_ptr<DecoderComponent> Player::Impl::createVideoDecoderComponent(std::string_view mediaInfo) noexcept {
    return nullptr;
}

void Player::Impl::pushAudioFrameDecode() noexcept {
    if (!info_.hasAudio) {
        return;
    }
    ///TODO: compare dts
    if (audioDecoder_ && audioDecoder_->isNeedPushFrame()) {
        if (!audioPackets_.empty()) {
            //LogI("push audio frame decode:{}", audioPackets_.front()->index);
            audioDecoder_->send(std::move(audioPackets_.front()));
            audioPackets_.pop_front();
        }
    }
}

void Player::Impl::pushVideoFrameDecode() noexcept {

}

void Player::Impl::pushAVFrameToRender() noexcept {
    if (!info_.hasAudio || !audioRender_ || audioRender_->isFull()) {
        return;
    }
    audioFrames_.withLock([this](auto& audioFrames){
        if (audioFrames.empty()) {
            return;
        }
        
        LogI("push audio frame render:{}", audioFrames.front()->index);
        audioRender_->send(std::move(audioFrames.front()));
        audioFrames.pop_front();
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
        pushAudioFrameDecode();
        demuxData();
    } else if (nowState == PlayerState::Buffering) {
        demuxData();
        pushAudioFrameDecode();
    }
    
}

void Player::Impl::doPlay() noexcept {
    ownerThread_->resume();
    readHandler_->resume();
    if (audioRender_) {
        audioRender_->play();
    }
    if (audioDecoder_) {
        audioDecoder_->resume();
    }
}

void Player::Impl::doPause() noexcept {
    ownerThread_->pause();
    readHandler_->pause();
    if (audioDecoder_) {
        audioDecoder_->pause();
    }
    if (audioRender_) {
        audioRender_->pause();
    }
}

void Player::Impl::doStop() noexcept {
    if (readHandler_) {
        readHandler_->close();
    }
    ownerThread_->stop();
    ownerThread_.reset();
    audioRender_.reset();
    audioDecoder_.reset();
}

void Player::Impl::doSeek() noexcept {
    if (!demuxer_) {
        return;
    }
    doPause();
    isReadCompleted_ = false;
    auto seekPos = demuxer_->getSeekToPos(seekRequest_.value().seekTime);
    readHandler_->seek(seekPos);
    demuxer_->seekPos(seekPos);
    audioRender_->seek(seekRequest_.value().seekTime);
    LogI("do seek pos:{}, time:{}", seekPos, seekRequest_.value().seekTime.second());
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
    readHandler_->resume();
    demuxer_->seekPos(seekPos);
    if (audioRender_) {
        audioRender_->seek(0);
    }
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
        auto size = std::any_cast<RenderSize>(t.data);
        params_.withWriteLock([size](auto& p){
            p->setting.size = size;
        });
        LogI("set size, width:{}, height:{}", size.width, size.height);
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
    if (changeState == PlayerState::Playing && isReadCompleted_ && isgreaterequal(nowTime, info_.duration)) {
        notifyTime(); //notify time to end
        LogI("play end.");
        bool isLoop;
        params_.withReadLock([&isLoop](auto& p){
            isLoop = p->setting.isLoop;
        });
        doLoop();
        if (!isLoop){
            changeState = PlayerState::Completed;
        }
    }
    if (changeState != PlayerState::Unknown) {
        setState(changeState);
    }
}

void Player::Impl::checkCacheStatus() noexcept {
    double time = 0;
    audioFrames_.withReadLock([](auto& frames) {
        if (frames.empty()) {
            return;
        }
    });
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
    double time = currentPlayedTime();
    observer_.withReadLock([this, time](auto& observer){
        if (auto ptr = observer.lock(); ptr) {
            ptr->notifyTime(playerId_, time);
        }
    });
    LogI("notifyTime:{}", time);
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
 
void Player::Impl::setRenderSize(RenderSize size) {
    auto ptr = buildEvent(EventType::UpdateSettingRenderSize);
    ptr->data = std::make_any<RenderSize>(size);
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
    double currentTime = 0;
    if (audioRender_) {
        currentTime = audioRender_->playedTime().second();
    }
    return currentTime;
}

}//end namespace slark
