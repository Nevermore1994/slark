//
//  PlayerImpl.hpp
//  slark
//
//  Created by Nevermore.
//

#pragma once

#include <deque>
#include <shared_mutex>
#include <optional>
#include "Reader.hpp"
#include "public/Player.h"
#include "DecoderComponent.h"
#include "DemuxerManager.h"
#include "AVFrame.hpp"
#include "Reader.hpp"
#include "Channel.hpp"
#include "Event.h"
#include "AudioRenderComponent.h"
#include "Synchronized.hpp"

namespace slark {

class Player::Impl {
public:
    explicit Impl(std::unique_ptr<PlayerParams> params);
    ~Impl();
    Impl(const Impl&) = delete;
    Impl& operator=(const Impl&) = delete;
public:
    void updateState(PlayerState state) noexcept;

    void updateSetting(PlayerSetting setting) noexcept;

    void seek(long double time) noexcept;

    void seek(long double time, bool isAccurate) noexcept;
    
    void addObserver(IPlayerObserverPtr observer) noexcept;
    
    void addObserver(IPlayerObserverPtrArray observers) noexcept;
    
    void removeObserver(const IPlayerObserverPtr& observer) noexcept;
    
    void removeObserver(const IPlayerObserverPtrArray& observers) noexcept;
public:
    [[nodiscard]] inline const PlayerInfo& info() const noexcept {
        return info_;
    }

    [[nodiscard]] inline PlayerState state() const noexcept {
        return state_;
    }

    [[nodiscard]] inline PlayerParams params()  noexcept {
        PlayerParams params;
        params_.withReadLock([&params](auto& p){
            params = *p;
        });
        return params;
    }

    [[nodiscard]] inline std::string_view playerId() const noexcept {
        return std::string_view(playerId_);
    }
    
private:
    void init() noexcept;

    void initPlayerInfo() noexcept;

    void demux() noexcept;
    
    void render() noexcept;

    void process() noexcept;
    
    void handleEvent(std::list<Event>&& events) noexcept;
    
    void notifyObserver(PlayerState state) noexcept;
    
    void setState(PlayerState state) noexcept;

    bool createDemuxer(DataPtr& data) noexcept;

    std::unique_ptr<DecoderComponent> createDecoderComponent(std::string_view mediaInfo, bool isAudio) noexcept;

    void decodeAudio() noexcept;

    void decodeVideo() noexcept;
private:
    bool isReadCompleted_ = false;
    PlayerState state_ = PlayerState::Unknown;
    std::optional<int64_t> seekPos_;
    PlayerInfo info_;
    std::string playerId_;
    Synchronized<std::unique_ptr<PlayerParams>, std::shared_mutex> params_;
    Synchronized<std::unordered_map<uint64_t, IPlayerObserverPtr>> observers_;
    
    SenderPtr<Event> sender_;
    ReceiverPtr<Event> receiver_;
    
    std::unique_ptr<Thread> ownerThread_ = nullptr;
    
    //IO
    std::unique_ptr<Reader> readHandler_ = nullptr;
    Synchronized<std::vector<DataPtr>> dataList_;
    DataPtr demuxData_;
    
    //demux
    std::unique_ptr<IDemuxer> demuxer_ = nullptr;
    std::deque<AVFramePtr> audioPackets_;
    std::deque<AVFramePtr> videoPackets_;
    
    //decode
    Synchronized<std::deque<AVFramePtr>> audioFrames_;
    Synchronized<std::deque<AVFramePtr>> videoFrames_;
    std::unique_ptr<DecoderComponent> audioDecoder_ = nullptr;
    std::unique_ptr<DecoderComponent> videoDecoder_ = nullptr;
    
    //render
    std::unique_ptr<Audio::AudioRenderComponent> audioRender_ = nullptr;
};

}
