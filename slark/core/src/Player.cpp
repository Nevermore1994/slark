//
//  Player.cpp
//  Slark
//
//  Created by Nevermore on 2022/4/22.
//


#include "Player.hpp"
#include "PlayerImpl.hpp"

namespace Slark {

Player::Player(const std::shared_ptr<PlayerParams>& params)
    : pimpl_(std::make_unique<Player::Impl>(params)) {

}

Player::~Player() {
    pimpl_->stop();
}

void Player::play() noexcept {
    pimpl_->play();
}

void Player::stop() noexcept {
    pimpl_->stop();
}

void Player::resume() noexcept {
    pimpl_->resume();
}

void Player::pause() noexcept {
    pimpl_->pause();
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

const PlayerParams& Player::observer() {
    return pimpl_->params();
}

PlayerState Player::state() {
    return pimpl_->state();
}

const PlayerInfos& Player::info() {
    return pimpl_->info();
}

}//end namespace Slark
