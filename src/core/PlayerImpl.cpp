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
        observers_.withLock([&](auto& map) {
            map[hash.value()] = std::move(observer);
        });
    }
}

void Player::Impl::addObserver(IPlayerObserverPtrArray observers) noexcept {
    std::unordered_map<uint64_t, IPlayerObserverPtr> tempMap;
    for (auto& ptr : observers) {
        if (auto hash = Util::getWeakPtrAddress(ptr); hash.has_value()) {
            tempMap[hash.value()] = std::move(ptr);
        }
    }
    observers_.withLock([temp = std::move(tempMap)](auto& map) {
        map.insert_range(temp);
    });
}

void Player::Impl::removeObserver(const IPlayerObserverPtr& observer) noexcept {
    if (auto hash = Util::getWeakPtrAddress(observer); hash.has_value()) {
        observers_.withLock([&hash](auto& map) {
            map.erase(hash.value());
        });
    }
}

void Player::Impl::removeObserver(const IPlayerObserverPtrArray& observers) noexcept {
    std::vector<uint64_t> keyArray;
    for (const auto& ptr : observers) {
        if (auto hash = Util::getWeakPtrAddress(ptr); hash.has_value()) {
            keyArray.emplace_back(hash.value());
        }
    }
    observers_.withLock([&](auto& map) {
        for (const auto& key : keyArray) {
            map.erase(key);
        }
    });
}

void Player::Impl::init() noexcept {
    setState(PlayerState::Initializing);
    auto [sp, rp] = Channel<EventPtr>::create();
    sender_ = std::move(sp);
    receiver_ = std::move(rp);

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
    readHandler_->open(path, std::move(setting));

    ownerThread_ = std::make_unique<Thread>("playerThread", &Player::Impl::process, this);
    setState(PlayerState::Ready);
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
        return true;
    }
    LogE("create demuxer error...");
    return false;
}

void Player::Impl::initPlayerInfo() noexcept {
    info_.hasAudio = demuxer_->hasAudio();
    info_.hasVideo = demuxer_->hasVideo();
    ///TODO: fix duration

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
            audioRender_->play();
        } while(false);
    } else {
        LogI("no audio");
    }

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
    LogI("receive data size: {}", dataList.size());
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
    if (!audioRender_ || !audioRender_->isFull()) {
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
    if (state_ == PlayerState::Stop) {
        return;
    }
    if (seekRequest_.has_value()) {
        doSeek();
    }
    if (state_ == PlayerState::Playing) {
        render();
        decodeAudio();
        demux();
    }
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
    if (state_ == state) {
        return;
    }
    state_ = state;
    if (state_ == PlayerState::Playing) {
        ownerThread_->resume();
        readHandler_->resume();
        if (audioRender_) {
            audioRender_->play();
        }
        if (audioDecoder_) {
            audioDecoder_->resume();
        }
    } else if (state_ == PlayerState::Pause) {
        readHandler_->pause();
        ownerThread_->pause();
        if (audioRender_) {
            audioRender_->pause();
        }
        if (audioDecoder_) {
            audioDecoder_->pause();
        }
    } else if (state_ == PlayerState::Stop) {
        readHandler_->close();
        ownerThread_->stop();
        audioRender_.reset();
        audioDecoder_.reset();
    }
    notifyObserver(state);
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

void Player::Impl::handleEvent(std::list<EventPtr>&& events) noexcept {
    PlayerState changeState = PlayerState::Unknown;
    for (auto& event : events) {
        if (!event) {
            continue;
        }
        if (event->type == EventType::Seek) {
            seekRequest_ = std::any_cast<PlayerSeekRequest>(event->data);
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

void Player::Impl::notifyObserver(PlayerState state) noexcept {
    observers_.withLock([this,state](auto& map){
        for (auto& p : std::views::values(map)
            | std::views::filter([](const IPlayerObserverPtr& p) { return !p.expired();})) {
            if (auto ref = p.lock(); ref) {
                ref->notifyState(playerId_,state);
            }
        }
        std::erase_if(map, [](auto& pair) {
            return pair.second.expired();
        });
    });
}

void Player::Impl::updateSetting(PlayerSetting setting) noexcept {
    params_.withWriteLock([&setting](auto& p){
        p->setting = setting;
    });
}

}//end namespace slark
