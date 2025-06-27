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
    : pimpl_(std::make_shared<Player::Impl>(std::move(params))) {
}

Player::~Player() = default;

void Player::prepare() noexcept {
    if (!pimpl_) {
        LogE("Player is not initialized.");
        return;
    }
    if (pimpl_->state() != PlayerState::Unknown) {
        LogI("Player is already prepared, state:{}", static_cast<int>(pimpl_->state()));
        return;
    }
    pimpl_->init();
}

void Player::play() noexcept {
    if (pimpl_->state() == PlayerState::Playing) {
        LogI("already playing.");
        return;
    }
    LogI("start playing.");
    pimpl_->updateState(PlayerState::Playing);
}

void Player::stop() noexcept {
    if (pimpl_->state() == PlayerState::Stop || pimpl_->state() == PlayerState::Unknown) {
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


void Player::seek(double time, bool isAccurate) noexcept {
    pimpl_->seek(time, isAccurate);
}

std::string_view Player::playerId() const noexcept {
    return pimpl_->playerId();
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

PlayerInfo Player::info() noexcept {
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

void Player::addObserver(IPlayerObserverPtr observer) noexcept {
    pimpl_->addObserver(std::move(observer));
}

void Player::removeObserver() noexcept {
    pimpl_->removeObserver();
}

double Player::currentPlayedTime() noexcept {
    if (!pimpl_) {
        return 0;
    }
    return pimpl_->currentPlayedTime();
}

void Player::setRenderImpl(std::weak_ptr<IVideoRender> render) noexcept {
    if (!pimpl_) {
        return;
    }
    pimpl_->setRenderImpl(render);
}

}//end namespace slark
