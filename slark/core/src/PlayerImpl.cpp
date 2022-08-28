//
//  PlayerImpl.cpp
//  slark
//
//  Created by Nevermore on 2022/5/4.
//

#include "Utility.hpp"
#include "FileUtility.hpp"
#include "Log.hpp"
#include "PlayerImpl.hpp"
#include <utility>
#include <functional>


namespace slark{

Player::Impl::Impl(std::shared_ptr<PlayerParams> params)
    : params_(std::move(params))
    , playerId_(Util::uuid()) {
    init();
}

Player::Impl::~Impl() {
    if(state_ != PlayerState::Stop){
        stop();
    }
}

void Player::Impl::play() noexcept {
    if(state_ == PlayerState::Playing){
        logi("Player is playing ...");
        return;
    }
    if(state_ == PlayerState::Initializing){
        logw("Player is initializing ...");
        return;
    }
    setState(PlayerState::Playing);
    transporter_->start();
    logi("Player start play ...");
}

void Player::Impl::stop() noexcept {
    if(state_ == PlayerState::Stop){
        logi("Player is stopped ...");
        return;
    }
    setState(PlayerState::Stop);
    transporter_->pause();
}

void Player::Impl::resume() noexcept {
    if(state_ != PlayerState::Pause){
        logi("Player is %d ...", static_cast<int>(state_));
        return;
    }
    setState(PlayerState::Playing);
    transporter_->resume();
}

void Player::Impl::pause() noexcept {
    if(state_ != PlayerState::Playing){
        logi("Player is %d ...", static_cast<int>(state_));
        return;
    }
    setState(PlayerState::Pause);
    transporter_->pause();
}

void Player::Impl::seek(long double time) noexcept {
    
}

void Player::Impl::seek(long double time, bool isAccurate) noexcept {

}

#pragma region

void Player::Impl::init() noexcept {
    setState(PlayerState::Initializing);
    std::vector<std::string> paths;
    for(const auto& item:params_->items){
        paths.push_back(item.path);
    }
    dataManager_ = std::make_unique<IOManager>(paths, 0, [this](std::unique_ptr<Data> data, uint64_t offset, IOState state){
        if(seekOffset_ != kInvalid && offset != seekOffset_){
            return;
        }
        
        {
            std::unique_lock<std::mutex> lock(mutex_);
            seekOffset_ = kInvalid;
            rawDatas_.push_back(std::move(data));
        }
    });
    listeners_.push_back(std::weak_ptr<ITransportObserver>(dataManager_));
    transporter_ = std::make_unique<Thread>("transporter", &Player::Impl::process, this);
    setState(PlayerState::Ready);
}

void Player::Impl::handleData(std::list<std::unique_ptr<Data>>&& dataList) noexcept {
    std::string demuxData;
    logi("handleData %ld", dataList.size());
    while(!dataList.empty()){
        auto data = std::move(dataList.front());
        dataList.pop_front();
        
        if(demuxer_ == nullptr){
            demuxData.append(data->data, data->length);
            std::string_view dataView(demuxData);
            auto demuxType = DemuxerManager::shareInstance().probeDemuxType(dataView);
            if(demuxType == DemuxerType::Unknown){
                continue;
            }
            
            demuxer_ = DemuxerManager::shareInstance().create(demuxType);
            auto [res, offset] = demuxer_->open(dataView);
            if(res){
                auto p = demuxData.data();
                data = std::make_unique<Data>(p + offset, demuxData.length() - offset);
                demuxData.clear();
            } else {
                loge("create demuxer error...");
            }
        }
        
        if(demuxer_ == nullptr || !demuxer_->isInited()){
            loge("not find demuxer...");
            continue;
        }
        auto&& [code, frameList] = demuxer_->parseData(std::move(data));
        //logi("demux frame count %d, state: %d", frameList.size(), code);
        if(code == DemuxerState::Failed && dataList.empty()){
            break;
        }
        rawFrames_.push(frameList);
    }
}

void Player::Impl::setState(PlayerState state) noexcept {
    if(state_ == state){
        return;
    }
    notifyEvent(state);
    state_ = state;
    if(!params_->observer.expired()){
        auto observer = params_->observer.lock();
        observer->updateState(state);
    }
}

void Player::Impl::process() {
    decltype(rawDatas_) dataList;
    {
        std::unique_lock<std::mutex> lock(mutex_);
        dataList.swap(rawDatas_);
    }
    handleData(std::move(dataList));
    
    auto state = dataManager_->state();
    //auto isLoop = params_->isLoop;
    if(state == IOState::Failed || state == IOState::Error){
        stop();
    } else if (state == IOState::Eof) {
        if(params_->isLoop){
            //dataManager_->setIndex(0);
        }
    }
    if(demuxer_ && demuxer_->isCompleted()){
        stop();
    }
}

TransportEvent Player::Impl::eventType(PlayerState state) const noexcept {
    auto event = TransportEvent::Unknown;
    switch(state) {
        case PlayerState::Playing:
            if(state_ == PlayerState::Ready) {
                event = TransportEvent::Start;
            } else if(state_ == PlayerState::Stop) {
                event = TransportEvent::Reset;
            } else if(state_ == PlayerState::Pause) {
                event = TransportEvent::Start;
            }
            break;
        case PlayerState::Pause:
            event = TransportEvent::Pause;
            break;
        case PlayerState::Stop:
            event = TransportEvent::Stop;
            break;
        default:
            break;
    }
    return event;
}

void Player::Impl::notifyEvent(PlayerState state) const noexcept {
    auto event = eventType(state);
    if(event == TransportEvent::Unknown){
        loge("transport event error.");
        return;
    }
    for(const auto& ptr : listeners_){
        if(ptr.expired()){
            continue;
        }
        auto listener = ptr.lock();
        listener->updateEvent(event);
    }
}

}//end namespace slark
#pragma  endregion
