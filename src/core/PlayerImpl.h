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
#include "Clock.h"

namespace slark {

struct PlayerSeekRequest {
    bool isAccurate = false;
    long double seekTime{0};
};

struct PlayerStatistics {
    bool isFirstAudioRendered = false;
    bool isForceVideoRendered = false;
    Time::TimePoint audioDecodeDelta{0};
    Time::TimePoint audioRenderDelta{0};
    long double audioDemuxedDuration = 0;
    long double videoDemuxedDuration = 0;
    
    void reset() {
        isFirstAudioRendered = false;
        isForceVideoRendered = false;
        audioDecodeDelta = 0;
        audioRenderDelta = 0;
        audioDemuxedDuration = 0;
        videoDemuxedDuration = 0;
    }
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
    
    void setRenderImpl(std::weak_ptr<IVideoRender>& render) noexcept;
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
    
    [[nodiscard]] void* requestRender() noexcept;
private:
    void init() noexcept;

    void initPlayerInfo() noexcept;

    void demuxData() noexcept;
    
    void pushAVFrameToRender() noexcept;
    
    void pushVideoFrameToRender() noexcept;

    void process() noexcept;
    
    void handleEvent(std::list<EventPtr>&& events) noexcept;
    
    void handleSettingUpdate(Event& t) noexcept;
    
    void notifyPlayerState(PlayerState state) noexcept;
    
    void notifyPlayerEvent(PlayerEvent event, std::string value = "") noexcept;
    
    void notifyPlayedTime() noexcept;
    
    void notifyCacheTime() noexcept;
    
    void setState(PlayerState state) noexcept;

    bool createDemuxer(IOData& data) noexcept;
    
    bool openDemuxer(IOData& data) noexcept;
    
    void createAudioComponent(const PlayerSetting& setting) noexcept;
    
    void createVideoComponent(const PlayerSetting& setting) noexcept;

    void pushAVFrameDecode() noexcept;
    
    void pushAudioFrameDecode() noexcept;

    void pushVideoFrameDecode(bool isForce = false) noexcept;
    
    void doPlay() noexcept;
    
    void doPause() noexcept;
    
    void doStop() noexcept;
    
    void doSeek(PlayerSeekRequest seekRequest) noexcept;
    
    void doLoop() noexcept;
    
    long double demuxedDuration() const noexcept;
    
    void checkCacheState() noexcept;
    
    bool setupIOHandler() noexcept;
    
    long double videoRenderTime() noexcept;
    
    long double audioRenderTime() noexcept;
private:
    bool isSeekingWhilePlaying_ = false;
    std::atomic_bool isStoped_ = false;
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
    Synchronized<std::weak_ptr<IVideoRender>, std::shared_mutex> videoRender_;
    PlayerStatistics statistics_;
    
    std::mutex releaseMutex_;
    std::condition_variable cond_;
};

}
