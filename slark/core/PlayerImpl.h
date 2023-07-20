//
//  PlayerImpl.hpp
//  slark
//
//  Created by Nevermore on 2022/5/4.
//

#pragma once

#include <deque>
#include <string_view>
#include "ReadHandler.hpp"
#include "Player.h"
#include "DecoderManager.h"
#include "DemuxerManager.h"
#include "AVFrame.hpp"
#include "IOManager.h"
#include "TransportEvent.h"
#include "Thread.h"
#include "AVFrameDeque.hpp"

namespace slark {

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
    [[nodiscard]] inline const PlayerInfos& info() const noexcept {
        return info_;
    }

    [[nodiscard]] inline PlayerState state() const noexcept {
        return state_;
    }

    [[nodiscard]] inline const PlayerParams& params() const noexcept {
        return *params_;
    }

    [[nodiscard]] inline std::string_view playerId() const noexcept {
        return std::string_view(playerId_);
    }

private:
    void init() noexcept;

    void demuxData() noexcept;
    
    void decodeData() noexcept;

    void setState(PlayerState state) noexcept;

    void process();

    void notifyEvent(PlayerState state) noexcept;

    [[nodiscard]] TransportEvent eventType(PlayerState state) const noexcept;

    void updateInternalState();
private:
    std::mutex dataMutex_;
    PlayerState state_ = PlayerState::Unknown;
    int64_t seekOffset_ = kInvalid;
    PlayerInfos info_;
    std::string playerId_;
    std::shared_ptr<PlayerParams> params_;
    std::unique_ptr<slark::Thread> transporter_ = nullptr;
    std::list<std::unique_ptr<Data>> dataList_;
    std::vector<std::weak_ptr<ITransportObserver>> listeners_;
    std::shared_ptr<IOManager> dataManager_ = nullptr;
    std::unique_ptr<IDemuxer> demuxer_ = nullptr;
    AVFrameSafeDeque rawPackets_;
    AVFrameSafeDeque decodeFrames_;
};

}
