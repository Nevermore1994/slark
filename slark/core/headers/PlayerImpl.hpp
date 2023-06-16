//
//  PlayerImpl.hpp
//  Slark
//
//  Created by Nevermore on 2022/5/4.
//

#pragma once

#include <deque>
#include <string_view>
#include "FileHandler.hpp"
#include "Player.hpp"
#include "DecoderManager.hpp"
#include "DemuxerManager.hpp"
#include "AVFrame.hpp"
#include "IOManager.hpp"
#include "TransportEvent.hpp"
#include "Thread.hpp"
#include "AVFrameDeque.hpp"

namespace Slark {

class Player::Impl {
public:
    explicit Impl(std::shared_ptr<PlayerParams> params);

    ~Impl();

public:
    void play() noexcept;

    void stop() noexcept;

    void pause() noexcept;

    void resume() noexcept;

    void seek(long double time) noexcept;

    void seek(long double time, bool isAccurate) noexcept;

public:
    inline const PlayerInfos& info() const noexcept {
        return info_;
    }

    inline PlayerState state() const noexcept {
        return state_;
    }

    inline const PlayerParams& params() const noexcept {
        return *params_;
    }

    inline std::string_view playerId() const noexcept {
        return std::string_view(playerId_);
    }

private:
    void init() noexcept;

    void handleData(std::list<std::unique_ptr<Data>>&& dataList) noexcept;

    void setState(PlayerState state) noexcept;

    void process();

    void notifyEvent(PlayerState state) const noexcept;

    TransportEvent eventType(PlayerState state) const noexcept;

private:
    std::mutex mutex_;
    PlayerState state_ = PlayerState::Unknown;
    int64_t seekOffset_ = kInvalid;
    PlayerInfos info_;
    std::string playerId_;
    std::shared_ptr<PlayerParams> params_;
    std::unique_ptr<Slark::Thread> transporter_ = nullptr;
    std::list<std::unique_ptr<Data>> rawDatas_;
    std::vector<std::weak_ptr<ITransportObserver>> listeners_;
    std::shared_ptr<IOManager> dataManager_ = nullptr;
    std::shared_ptr<IDemuxer> demuxer_ = nullptr;
    AVFrameSafeDeque rawFrames_;
    AVFrameSafeDeque decodeFrames_;
};

}
