//
//  Player.cpp
//  slark
//
//  Created by Nevermore on 2022/4/22.
//

#include "Player.h"

#include <utility>
#include "PlayerImpl.h"
#include "Log.hpp"

namespace slark {

Player::Player(std::unique_ptr<PlayerParams> params)
    : pimpl_(std::make_unique<Player::Impl>(std::move(params))) {

}

Player::~Player() = default;

void Player::play() noexcept {
    if (pimpl_->state() == PlayerState::Playing) {
        LogI("already playing.");
        return;
    }
    LogI("start playing.");
    pimpl_->updateState(PlayerState::Playing);
}

void Player::stop() noexcept {
    if (pimpl_->state() == PlayerState::Stop) {
        LogI("already stopped.");
        return;
    }
    LogI("stop playing.");
    pimpl_->updateState(PlayerState::Stop);
}

void Player::pause() noexcept {
    auto nowState = state();
    if (nowState != PlayerState::Playing) {
        LogI("not playing, state:{}", static_cast<int>(nowState));
        return;
    }
    LogI("pause playing.");
    pimpl_->updateState(PlayerState::Pause);
}


void Player::seek(long double time) noexcept {
    pimpl_->seek(time);
}

std::string_view Player::playerId() const noexcept {
    return pimpl_->playerId();
}

void Player::seek(long double time, bool isAccurate) noexcept {
    pimpl_->seek(time, isAccurate);
}

PlayerParams Player::peek() noexcept {
    return pimpl_->params();
}

PlayerState Player::state() noexcept {
    return pimpl_->state();
}

const PlayerInfo& Player::info() noexcept {
    return pimpl_->info();
}

void Player::updateSetting(PlayerSetting setting) noexcept {

}

void Player::addObserver(IPlayerObserverPtr observer) noexcept {
    pimpl_->addObserver(std::move(observer));
}

void Player::addObserver(IPlayerObserverPtrArray observers) noexcept {
    pimpl_->addObserver(std::move(observers));
}

void Player::removeObserver(const IPlayerObserverPtr& observer) noexcept {
    pimpl_->removeObserver(observer);
}

void Player::removeObserver(const IPlayerObserverPtrArray& observers) noexcept {
    pimpl_->removeObserver(observers);
}

}//end namespace slark
