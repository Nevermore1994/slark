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
#include "Player.h"
#include "Log.hpp"
#include "PlayerImpl.h"
#include "Util.hpp"

namespace slark {

Player::Impl::Impl(std::unique_ptr<PlayerParams> params)
    : playerId_(Random::uuid()) {
    params_.withWriteLock([&params](auto& p){
        p = std::move(params);
    });
    init();
}

Player::Impl::~Impl() {
    ownerThread_->stop();
    if (readHandler_) {
        readHandler_->close();
    }
}


void Player::Impl::seek(long double time, bool isAccurate) noexcept {
    auto ptr = buildEvent(EventType::Seek);
    PlayerSeekRequest req;
    req.seekTime = CTime(time);
    req.isAccurate = isAccurate;
    ptr->data = req;
    sender_->send(std::move(ptr));
}

void Player::Impl::updateState(PlayerState state) noexcept {
    sender_->send(buildEvent(state));
    ownerThread_->resume();
}

void Player::Impl::addObserver(IPlayerObserverPtr observer) noexcept {
    if (auto hash = Util::getWeakPtrAddress(observer); hash.has_value()) {
        observer_.withWriteLock([&hash, observer = std::move(observer)](auto& ob) {
            ob = std::move(observer);
        });
    }
}

void Player::Impl::removeObserver(IPlayerObserverPtr observer) noexcept {
    if (auto hash = Util::getWeakPtrAddress(observer); hash.has_value()) {
        observer_.withWriteLock([&hash](auto& observer) {
            observer.reset();
        });
    }
}

void Player::Impl::init() noexcept {
    using namespace std::chrono_literals;
    setState(PlayerState::Initializing);
    auto [sp, rp] = Channel<EventPtr>::create();
    sender_ = std::move(sp);
    receiver_ = std::move(rp);

    ownerThread_ = std::make_unique<Thread>("playerThread", &Player::Impl::process, this);
    ownerThread_->runLoop(500ms, [this](){
        if (state() == PlayerState::Playing) {
            notifyTime(); //0.5s notify observer
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
    setting.callBack = [this](DataPtr data, int64_t offset, IOState state) {
        if (seekRequest_.has_value() && seekRequest_.value().seekPos != offset) {
            return;
        }
        LogI("receive data size: {}", data->length);
        if (data) {
            dataList_.withLock([&](auto& dataList) {
                dataList.emplace_back(std::move(data));
            });
        }

        if (state == IOState::Error) {
            sender_->send(buildEvent(EventType::ReadError));
        } else if (state == IOState::EndOfFile) {
            sender_->send(buildEvent(EventType::ReadCompleted));
        }
    };
    readHandler_ = std::make_unique<Reader>();
    if (!readHandler_->open(path, std::move(setting))) {
        setState(PlayerState::Error);
        notifyEvent(PlayerEvent::OnError, std::to_string(static_cast<uint8_t>(PlayerErrorCode::OpenFileError)));
    } else {
        setState(PlayerState::Buffering);
        readHandler_->start();
        LogI("player initializing start.");
    }
    
    ownerThread_->start();
}

bool Player::Impl::createDemuxer(DataPtr& data) noexcept {
    if (demuxData_ == nullptr) {
        demuxData_ = std::make_unique<Data>();
    }
    demuxData_->append(std::move(data));

    auto dataView = demuxData_->view();
    auto demuxType = DemuxerManager::shareInstance().probeDemuxType(dataView);
    if (demuxType == DemuxerType::Unknown) {
        return false;
    }

    demuxer_ = DemuxerManager::shareInstance().create(demuxType);
    if (!demuxer_) {
        LogE("not found demuer type:{}", static_cast<int32_t>(demuxType));
        return false;
    }
    if (auto [res, offset] = demuxer_->open(dataView); res) {
        auto p = demuxData_->rawData;
        data = std::make_unique<Data>(demuxData_->length - offset, p + offset);
        demuxData_.reset();
        initPlayerInfo();
        LogI("init demuxer success.");
        return true;
    }
    LogE("create demuxer error...");
    return false;
}

void Player::Impl::initPlayerInfo() noexcept {
    info_.hasAudio = demuxer_->hasAudio();
    info_.hasVideo = demuxer_->hasVideo();
    info_.duration = demuxer_->totalDuration().second();
    if (info_.hasAudio) {
        LogI("has audio, audio info:{}", demuxer_->audioInfo()->mediaInfo);
        do {
            audioDecoder_ = createDecoderComponent(demuxer_->audioInfo()->mediaInfo, true);
            if (!audioDecoder_) {
                LogI("create decoder error:{}", demuxer_->audioInfo()->mediaInfo);
                break;
            }
            audioRender_ = std::make_unique<Audio::AudioRenderComponent>(demuxer_->audioInfo());
            audioRender_->renderCompletion = [this](CTime audioPlayedTime){
                playedTime.withLock([audioPlayedTime](auto& time){
                    time.audioPlayedTime = audioPlayedTime;
                    LogI("audio played time:{}", audioPlayedTime.second());
                });
            };
        } while(false);
    } else {
        LogI("no audio");
    }
    setState(PlayerState::Ready);
    LogE("init player success.");
}

void Player::Impl::demux() noexcept {
    if (demuxer_ && demuxer_->isCompleted()) {
        return;
    }
    std::vector<DataPtr> dataList;
    dataList_.withLock([&](auto& list) {
        list.swap(dataList);
    });
    if (dataList.empty()) {
        return;
    }
    LogI("demux data size:{}", dataList.size());
    for (auto& data : dataList) {
        if (demuxer_ == nullptr && !createDemuxer(data)) {
            LogI("create demuxer failed.");
            continue;
        }

        if (demuxer_ == nullptr || !demuxer_->isInited()) {
            LogE("not find demuxer.");
            continue;
        }
        auto parseResult = demuxer_->parseData(std::move(data));
        //LogI("demux frame count %d, state: %d", frameList.size(), code);
        if (parseResult.resultCode == DemuxerResultCode::Failed && dataList.empty()) {
            break;
        }
        if (!parseResult.audioFrames.empty()) {
            for (auto& packet : parseResult.audioFrames) {
                LogI("demux audio frame:{}", packet->index);
                audioPackets_.push_back(std::move(packet));
            }
        }
        if (!parseResult.videoFrames.empty()) {
            for (auto& packet : parseResult.videoFrames) {
                videoPackets_.push_back(std::move(packet));
            }
        }
    }
}

std::unique_ptr<DecoderComponent> Player::Impl::createDecoderComponent(std::string_view mediaInfo, bool isAudio) noexcept {
    PlayerSetting setting;
    params_.withReadLock([&setting](auto& p){
        setting = p->setting;
    });
    auto decodeType = DecoderManager::shareInstance().getDecoderType(mediaInfo, isAudio ? setting.enableAudioSoftDecode :setting.enableVideoSoftDecode);
    if (!DecoderManager::shareInstance().contains(decodeType)) {
        LogI("not found decoder:media info {}  type:{}", mediaInfo, static_cast<int32_t>(decodeType));
        return nullptr;
    }
    auto& frames = isAudio ? audioFrames_ : videoFrames_;
    auto decoder = std::make_unique<DecoderComponent>(decodeType, [&frames](auto frameArray) {
        LogI("receive frame size: {}", frameArray.size());
        frames.withLock([&frameArray](auto& frames) {
            std::ranges::for_each(frameArray, [&frames] (auto& frame) { frames.push_back(std::move(frame)); });
        });
    });
    return decoder;
}

void Player::Impl::decodeAudio() noexcept {
    if (!info_.hasAudio) {
        return;
    }
    if (audioDecoder_ && audioDecoder_->isNeedPushFrame()) {
        if (!audioPackets_.empty()) {
            LogI("push audio frame decode:{}", audioPackets_.front()->index);
            audioDecoder_->send(std::move(audioPackets_.front()));
            audioPackets_.pop_front();
        }
    }
}

void Player::Impl::decodeVideo() noexcept {

}

void Player::Impl::render() noexcept {
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
    if (seekRequest_.has_value()) {
        doSeek();
    }
    if (nowState == PlayerState::Playing) {
        render();
        decodeAudio();
        demux();
    } else if (nowState == PlayerState::Buffering) {
        demux();
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
        audioDecoder_->pause();
    }
}

void Player::Impl::doStop() noexcept {
    readHandler_->close();
    ownerThread_->stop();
    audioRender_.reset();
    audioDecoder_.reset();
}

void Player::Impl::doSeek() noexcept {
    if (!demuxer_) {
        return;
    }
    readHandler_->pause();
    seekRequest_.value().seekPos = demuxer_->getSeekToPos(seekRequest_.value().seekTime);
    LogI("seek pos:{}", seekRequest_.value().seekPos);
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
    } else if (state == PlayerState::Pause) {
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
    double time = 0;
    playedTime.withReadLock([&time](auto& playedTime){
        time = playedTime.time().second();
    });
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
    long double currentTime = 0;
    playedTime.withReadLock([&currentTime](auto& time){
        currentTime = time.time().second();
    });
    return currentTime;
}

}//end namespace slark
