//
//  PlayerImpl.hpp
//  slark
//
//  Created by Nevermore.
//

#pragma once

#include <deque>
#include <map>
#include "DecoderComponent.h"
#include "DemuxerManager.h"
#include "IReader.h"
#include "Channel.hpp"
#include "Event.h"
#include "AudioRenderComponent.h"
#include "Synchronized.hpp"
#include "Clock.h"
#include "PlayerImplHelper.h"

namespace slark {

struct PlayerSeekRequest {
    bool isAccurate = false;
    double seekTime{0};
};

struct PlayerStats {
    bool isForceVideoRendered = false;
    bool isAudioRenderEnd = false;
    bool isVideoRenderEnd = false;
    double audioDemuxedTime = 0;
    double videoDemuxedTime = 0;
    double lastNotifyPlayedTime = 0;
    double lastNotifyCacheTime = 0;

    void reset() noexcept {
        isForceVideoRendered = false;
        isVideoRenderEnd = false;
        isAudioRenderEnd = false;
        audioDemuxedTime = 0;
        videoDemuxedTime = 0;
        lastNotifyPlayedTime = 0;
        lastNotifyCacheTime = 0;
    }

    void setSeekTime(double seekTime) noexcept {
        audioDemuxedTime = seekTime;
        videoDemuxedTime = seekTime;
    }

    [[nodiscard]] bool isRenderEnd() const noexcept {
        return isVideoRenderEnd && isAudioRenderEnd;
    }
};

class Player::Impl: public std::enable_shared_from_this<Player::Impl> {
public:
    friend class PlayerImplHelper;

    explicit Impl(std::unique_ptr<PlayerParams> params);

    ~Impl();

    Impl(const Impl&) = delete;

    Impl& operator=(const Impl&) = delete;
public:
    void init() noexcept;
    
    void updateState(PlayerState state) noexcept;

    void setLoop(bool isLoop);
    
    void setVolume(float volume);
    
    void setMute(bool isMute);

    void seek(double time, bool isAccurate) noexcept;
    
    void addObserver(IPlayerObserverPtr observer) noexcept;
    
    void removeObserver() noexcept;
    
    void setRenderImpl(std::weak_ptr<IVideoRender> render) noexcept;
public:
    [[nodiscard]] inline PlayerInfo info() const noexcept {
        return info_;
    }

    [[nodiscard]] PlayerState state() noexcept;

    [[nodiscard]] PlayerParams params() noexcept;

    [[nodiscard]] inline std::string_view playerId() const noexcept {
        return std::string_view(playerId_);
    }
    
    [[nodiscard]] double currentPlayedTime() noexcept;
    
    [[nodiscard]] AVFrameRefPtr requestRender() noexcept;

private:

    void preparePlayerInfo() noexcept;

    void demuxData() noexcept;
    
    void handleAudioPacket(AVFramePtrArray& audioPackets) noexcept;
    
    void handleVideoPacket(AVFramePtrArray& videoPackets) noexcept;
    
    void pushAVFrameToRender() noexcept;

    void pushAudioFrameToRender() noexcept;

    void pushVideoFrameToRender() noexcept;

    void process() noexcept;
    
    void handleEvent(std::list<EventPtr>&& events) noexcept;
    
    void handleSettingUpdate(Event& t) noexcept;
    
    void notifyPlayerState(PlayerState state) noexcept;
    
    void notifyPlayerEvent(PlayerEvent event, std::string value = "") noexcept;
    
    void notifyPlayedTime() noexcept;
    
    void notifyCacheTime() noexcept;
    
    void setState(PlayerState state) noexcept;

    bool createDemuxer(DataPacket& data) noexcept;
    
    bool openDemuxer(DataPacket& data) noexcept;
    
    void createAudioComponent(const PlayerSetting& setting) noexcept;
    
    void createVideoComponent(const PlayerSetting& setting) noexcept;

    void pushAVFrameDecode() noexcept;
    
    void pushAudioPacketDecode() noexcept;

    void pushVideoPacketDecode(bool isForce = false) noexcept;
    
    void doPlay() noexcept;
    
    void doPause() noexcept;
    
    void doStop() noexcept;
    
    void doSeek(PlayerSeekRequest seekRequest) noexcept;
    
    void doLoop() noexcept;

    void clearData() noexcept;
    
    double demuxedDuration() const noexcept;
    
    void checkCacheState() noexcept;
    
    bool setupDataProvider() noexcept;
    
    double videoRenderTime() noexcept;
    
    double audioRenderTime() noexcept;

    void setVideoRenderTime(double time) noexcept;

    bool isVideoNeedDecode(double renderedTime) noexcept;

    bool isAudioNeedDecode(double renderedTime) noexcept;
private:
    bool resumeAfterSeek_ = false;
    std::atomic_bool isStopped_ = false;
    std::atomic_bool isReleased_ = false;
    std::unique_ptr<PlayerImplHelper> helper_ = nullptr;
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
    std::unique_ptr<IReader> dataProvider_ = nullptr;
    Synchronized<std::deque<DataPacket>> dataList_;
    
    //demux
    std::shared_ptr<IDemuxer> demuxer_ = nullptr;
    std::deque<AVFramePtr> audioPackets_;
    std::deque<AVFramePtr> videoPackets_;
    
    //decoded frames
    Synchronized<std::deque<AVFrameRefPtr>> audioFrames_;
    Synchronized<std::map<int64_t, AVFrameRefPtr>> videoFrames_;
    std::shared_ptr<DecoderComponent> audioDecodeComponent_ = nullptr;
    std::shared_ptr<DecoderComponent> videoDecodeComponent_ = nullptr;
    
    //render
    std::unique_ptr<AudioRenderComponent> audioRender_ = nullptr;
    AtomicWeakPtr<IVideoRender> videoRender_;
    PlayerStats stats_;
    
    std::mutex releaseMutex_;
    std::condition_variable cond_;
};

}
