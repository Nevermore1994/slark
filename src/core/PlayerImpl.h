//
//  PlayerImpl.hpp
//  slark
//
//  Created by Nevermore.
//

#pragma once

#include "Channel.hpp"
#include "DemuxerComponent.h"
#include "DemuxerManager.h"
#include "Event.h"
#include "MediaSourceManager.h"
#include "NotifyCenter.h"
#include "Player.h"
#include "Synchronized.hpp"
#include "pipeline/PlayPipeline.h"

#include <atomic>
#include <memory>

namespace slark {

struct PlayerSeekRequest {
    bool isAccurate = false;
    double seekTime{0};
    Time::TimePoint startTime;
    uint64_t seekEpoch{0};
};

class Player::Impl : public std::enable_shared_from_this<Player::Impl> {
public:
    explicit Impl(std::unique_ptr<PlayerParams> params);

    ~Impl();

    Impl(const Impl&) = delete;

    Impl& operator=(const Impl&) = delete;

public:
    void init() noexcept;

    void updateState(PlayerState state) noexcept;

    void setLoop(bool isLoop) noexcept;

    void setVolume(float volume) noexcept;

    void setMute(bool isMute) noexcept;

    void seek(double time, bool isAccurate) noexcept;

    void addObserver(IPlayerObserverPtr observer) noexcept;

    void removeObserver() noexcept;

    void setRenderImpl(std::weak_ptr<IVideoRender> render) noexcept;

public:
    /// @note info_ 仅在 ownerThread 的 preparePlayerInfo() 中写入，调用方应在收到 Prepared
    /// 回调后再访问
    [[nodiscard]] inline PlayerInfo info() const noexcept {
        return info_;
    }

    [[nodiscard]] PlayerState state() const noexcept;

    [[nodiscard]] PlayerParams params() noexcept;

    [[nodiscard]] inline std::string_view playerId() const noexcept {
        return std::string_view(playerId_);
    }

    [[nodiscard]] double currentPlayedTime() const noexcept;

    [[nodiscard]] AVFrameRefPtr requestRender() noexcept;

private:
    void buildPipeline() noexcept;

    void handlePipelineEvent(PipelineEvent event, std::optional<PipelineError> error) noexcept;

    void preparePlayerInfo() noexcept;

    void process() noexcept;

    void handleEvent(std::list<EventPtr>&& events) noexcept;

    void handleSettingUpdate(Event& t) noexcept;

    void notifyPlayerEvent(PlayerEvent event, std::string value = {}) noexcept;

    void notifyPlayerState(PlayerState state) noexcept;

    void notifyPlayedTime(bool isEndTime = false) noexcept;

    void notifyCacheTime() noexcept;

    void setState(PlayerState state) noexcept;

    bool createDemuxerComponent() noexcept;

    void createAudioComponent(const PlayerSetting& setting) noexcept;

    void createVideoComponent(const PlayerSetting& setting) noexcept;

    void doPlay() noexcept;

    void doPause() noexcept;

    void doStop() noexcept;

    void doSeek(PlayerSeekRequest seekRequest) noexcept;

    void doLoop() noexcept;

    void resumeFromBuffering() noexcept;

    bool setupDataProvider() noexcept;

    [[nodiscard]] double demuxedDuration() const noexcept;

private:
    std::atomic_bool isStopped_ = false;
    std::atomic_bool isReleased_ = false;
    Synchronized<PlayerState, std::shared_mutex> state_;
    AtomicSharedPtr<PlayerSeekRequest> seekRequest_;
    PlayerInfo info_;
    std::string playerId_;
    Synchronized<std::unique_ptr<PlayerParams>, std::shared_mutex> params_;
    SenderPtr<EventPtr> sender_;
    ReceiverPtr<EventPtr> receiver_;

    std::shared_ptr<Thread> ownerThread_ = nullptr;

    NotifyCenter notifyCenter_;
    MediaSourceManager mediaSourceManager_;
    std::shared_ptr<DemuxerComponent> demuxerComponent_ = nullptr;
    std::shared_ptr<PlayPipeline> pipeline_ = nullptr;
    SenderPtr<DataPacket> dataPacketSender_;

    AtomicWeakPtr<IVideoRender> videoRender_;

    struct PlayerStats {
        bool resumeAfterBuffering = false;

        void reset() noexcept {
            resumeAfterBuffering = false;
        }
    };

    PlayerStats stats_;

    std::mutex releaseMutex_;
    std::condition_variable cond_;
};

} // namespace slark
