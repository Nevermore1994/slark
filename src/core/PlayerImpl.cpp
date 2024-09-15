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
#include "Utility.hpp"

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


void Player::Impl::seek(long double /*time*/) noexcept {
}

void Player::Impl::seek(long double /*time*/, bool /*isAccurate*/) noexcept {
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
    auto [sp, rp] = Channel<Event>::create();
    sender_ = std::move(sp);
    receiver_ = std::move(rp);

    ///set file path
    std::string_view path;
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
        if (seekPos_.has_value() && seekPos_.value() != offset) {
            return;
        }

        if (data) {
            dataList_.withLock([&](auto& dataList) {
                dataList.emplace_back(std::move(data));
            });
        }

        if (state == IOState::Error) {
            sender_->send(Event::ReadError);
        } else if (state == IOState::EndOfFile) {
            sender_->send(Event::ReadCompleted);
        }
    };
    readHandler_ = std::make_unique<Reader>(std::string(path), std::move(setting), "playItem");

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
    if (info_.hasAudio && info_.hasVideo) {
        info_.duration = std::max(demuxer_->audioInfo()->duration.second(), demuxer_->videoInfo()->duration.second());
    } else if (info_.hasAudio) {
        info_.duration = demuxer_->audioInfo()->duration.second();
    } else if (info_.hasVideo) {
        info_.duration = demuxer_->videoInfo()->duration.second();
    }

    if (info_.hasAudio) {
        LogI("has audio, audio info:{}", demuxer_->audioInfo()->mediaInfo);
        do {
            audioDecoder_ = createDecoderComponent(demuxer_->audioInfo()->mediaInfo, true);
            if (!audioDecoder_) {
                LogI("create decoder error:{}", demuxer_->audioInfo()->mediaInfo);
                break;
            }
            audioRender_ = std::make_unique<Audio::AudioRenderComponent>(demuxer_->audioInfo());
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
        audioDecoder_->send(std::move(audioPackets_.front()));
    }
}

void Player::Impl::decodeVideo() noexcept {

}

void Player::Impl::render() noexcept {
    if (info_.hasAudio && !audioRender_->isFull()) {
        audioFrames_.withLock([this](auto& audioFrames){
            audioDecoder_->send(std::move(audioFrames.front()));
            audioFrames.pop_front();
        });
    }
}

void Player::Impl::process() noexcept {
    handleEvent(receiver_->tryReceiveAll());
    if (state_ == PlayerState::Pause || state_ == PlayerState::Stop) {
        return;
    }
    render();
    demux();
}

void Player::Impl::setState(PlayerState state) noexcept {
    if (state_ == state) {
        return;
    }
    state_ = state;
    if (state_ == PlayerState::Playing) {
        ownerThread_->resume();
        readHandler_->resume();
    } else if (state_ == PlayerState::Pause) {
        readHandler_->pause();
        ownerThread_->pause();
    } else if (state_ == PlayerState::Stop) {
        readHandler_->close();
        ownerThread_->stop();
    }
    notifyObserver(state);
}

std::expected<PlayerState, bool> getStateFromEvent(Event event) {
    switch (event) {
        case Event::Play:
            return PlayerState::Playing;
        case Event::Pause:
            return PlayerState::Pause;
        case Event::Stop:
            return PlayerState::Stop;
        case Event::ReadError:
        case Event::DecodeError:
        case Event::DemuxError:
        case Event::RenderError:
            return PlayerState::Pause;
        case Event::ReadCompleted:
        case Event::Unknown:
        case Event::DemuxCompleted:
            return PlayerState::Unknown;
        case Event::RenderFrameCompleted:
            return PlayerState::Stop;
        default:
            return std::unexpected(false);
    }
}

void Player::Impl::handleEvent(std::list<Event>&& events) noexcept {
    PlayerState changeState = PlayerState::Unknown;
    for (auto& event : events) {
        if (auto state = getStateFromEvent(event); state.has_value()) {
            LogI("eventType:{}", EventTypeToString(event));
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
