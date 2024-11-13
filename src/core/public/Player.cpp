//
//  Player.cpp
//  slark
//
//  Created by Nevermore on 2022/4/22.
//

#include <utility>
#include "Player.h"
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

PlayerSetting Player::peekSetting() noexcept {
    return pimpl_->params().setting;
}

PlayerState Player::state() noexcept {
    return pimpl_->state();
}

const PlayerInfo& Player::info() noexcept {
    return pimpl_->info();
}

void Player::setLoop(bool isLoop) {
    pimpl_->setLoop(isLoop);
}

void Player::setVolume(float volume) {
    pimpl_->setVolume(volume);
}

void Player::setMute(bool isMute) {
    pimpl_->setMute(isMute);
}
 
void Player::setRenderSize(uint32_t width, uint32_t height) {
    pimpl_->setRenderSize(width, height);
}

void Player::addObserver(IPlayerObserverPtr observer) noexcept {
    pimpl_->addObserver(std::move(observer));
}

void Player::removeObserver() noexcept {
    pimpl_->removeObserver();
}

long double Player::currentPlayedTime() noexcept {
    if (!pimpl_) {
        return 0;
    }
    return pimpl_->currentPlayedTime();
}

void* Player::requestRender() noexcept {
    if (!pimpl_) {
        return nullptr;
    }
    return pimpl_->requestRender();
}

}//end namespace slark
