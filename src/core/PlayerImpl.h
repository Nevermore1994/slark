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
#include <map>
#include "Reader.hpp"
#include "Player.h"
#include "DecoderComponent.h"
#include "DemuxerManager.h"
#include "AVFrame.hpp"
#include "Reader.hpp"
#include "Channel.hpp"
#include "Event.h"
#include "AudioRenderComponent.h"
#include "Synchronized.hpp"
#include "Buffer.hpp"

namespace slark {

struct PlayerSeekRequest {
    bool isAccurate = false;
    Time::TimePoint seekTime{0};
};

struct PlayerStatistics {
    bool isFirstAudioRendered = false;
    bool isFirstVideoRendered = false;
    Time::TimePoint audioDecodeDelta{0};
    Time::TimePoint videoDecodeDelta{0};
    Time::TimePoint audioRenderDelta{0};
    Time::TimePoint videoRenderDelta{0};
};

class Player::Impl {
public:
    explicit Impl(std::unique_ptr<PlayerParams> params);
    ~Impl();
    Impl(const Impl&) = delete;
    Impl& operator=(const Impl&) = delete;
public:
    void updateState(PlayerState state) noexcept;

    void setLoop(bool isLoop);
    
    void setVolume(float volume);
    
    void setMute(bool isMute);
     
    void setRenderSize(uint32_t width, uint32_t height) noexcept;

    void seek(long double time, bool isAccurate = false) noexcept;
    
    void addObserver(IPlayerObserverPtr observer) noexcept;
    
    void removeObserver() noexcept;
public:
    [[nodiscard]] inline const PlayerInfo& info() const noexcept {
        return info_;
    }

    [[nodiscard]] PlayerState state() noexcept;

    [[nodiscard]] PlayerParams params() noexcept;

    [[nodiscard]] inline std::string_view playerId() const noexcept {
        return std::string_view(playerId_);
    }
    
    [[nodiscard]] long double currentPlayedTime() noexcept;
    
private:
    void init() noexcept;

    void initPlayerInfo() noexcept;

    void demuxData() noexcept;
    
    void pushAVFrameToRender() noexcept;

    void process() noexcept;
    
    void handleEvent(std::list<EventPtr>&& events) noexcept;
    
    void handleSettingUpdate(Event& t) noexcept;
    
    void notifyState(PlayerState state) noexcept;
    
    void notifyEvent(PlayerEvent event, std::string value = "") noexcept;
    
    void notifyTime() noexcept;
    
    void setState(PlayerState state) noexcept;

    bool createDemuxer(IOData& data) noexcept;
    
    bool openDemuxer(IOData& data) noexcept;

    void pushFrameDecode() noexcept;
    
    void pushAudioFrameDecode() noexcept;

    void pushVideoFrameDecode() noexcept;
    
    void doPlay() noexcept;
    
    void doPause() noexcept;
    
    void doStop() noexcept;
    
    void doSeek() noexcept;
    
    void doLoop() noexcept;
    
    [[nodiscard]] uint32_t demuxedDuration() noexcept;
    
    void checkCacheState() noexcept;
private:
    std::atomic_bool isReadCompleted_ = false;
    std::atomic_bool isRenderCompleted_ = false;
    Synchronized<PlayerState, std::shared_mutex> state_;
    std::optional<PlayerSeekRequest> seekRequest_;
    PlayerInfo info_;
    std::string playerId_;
    Synchronized<std::unique_ptr<PlayerParams>, std::shared_mutex> params_;
    Synchronized<IPlayerObserverPtr, std::shared_mutex> observer_;
    
    SenderPtr<EventPtr> sender_;
    ReceiverPtr<EventPtr> receiver_;
    
    std::unique_ptr<Thread> ownerThread_ = nullptr;
    
    //IO
    std::unique_ptr<Reader> readHandler_ = nullptr;
    Synchronized<std::vector<IOData>> dataList_;
    std::unique_ptr<Buffer> probeBuffer_;
    
    //demux
    std::unique_ptr<IDemuxer> demuxer_ = nullptr;
    std::deque<AVFramePtr> audioPackets_;
    std::deque<AVFramePtr> videoPackets_;
    
    //decode
    Synchronized<std::deque<AVFramePtr>, std::shared_mutex> audioFrames_;
    Synchronized<std::deque<AVFramePtr>, std::shared_mutex> videoFrames_;
    std::unique_ptr<DecoderComponent> audioDecodeComponent_ = nullptr;
    std::unique_ptr<DecoderComponent> videoDecodeComponent_ = nullptr;
    
    //render
    std::unique_ptr<AudioRenderComponent> audioRender_ = nullptr;
    PlayerStatistics statistics_;
};

}
