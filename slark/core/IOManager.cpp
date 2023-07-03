//
// Created by Nevermore on 2022/5/27.
// slark IOManager
// Copyright (c) 2022 Nevermore All rights reserved.
//
#include "IOManager.h"
#include "MediaUtility.hpp"
#include "Log.hpp"


namespace slark {

IOManager::IOManager(std::vector<std::string> paths, int16_t index, IOHandlerCallBack func)
    : state_(IOState::Normal)
    , index_(index)
    , paths_(std::move(paths))
    , callBack_(std::move(func)) {
}

IOManager::~IOManager() {
    if (handler_) {
        std::unique_lock<std::mutex> lock(mutex_);
        handler_->close();
        handler_.reset();
    }
}

bool IOManager::open() {
    std::unique_lock<std::mutex> lock(mutex_);
    if (!CheckIndexValid(index_, paths_)) {
        return false;
    }
    auto path = paths_[static_cast<size_t>(index_)];
    if (handler_ == nullptr) {
        buildHandler(path);
    } else {
        handler_->close();
    }
    return handler_->open(path);
}

bool IOManager::setIndex(int16_t index) noexcept {
    if (!CheckIndexValid(index, paths_)) {
        index_ = kInvalid;
        return false;
    }
    index_ = index;
    open();
    return true;
}

void IOManager::buildHandler(std::string path) {
    auto url = std::move(path);
    if (isLocalFile(url)) {
        handler_ = std::make_unique<FileHandler>();
    } else {
        //network
    }
    if (handler_) {
        handler_->setCallBack([this](std::unique_ptr<Data> data, int64_t offset, IOState state) {
            handleData(std::move(data), offset, state);
        });
    }
}

void IOManager::handleData(std::unique_ptr<Data> data, int64_t offset, IOState state) {
    if (data->length == 0 && state != IOState::EndOfFile) {
        LogI("[IOManager] offset %lld, io state %d", offset, static_cast<int>(state));
    }
    offset_ = static_cast<uint64_t>(offset) + data->length;
    state_ = state;
    if (state == IOState::EndOfFile && nextTask()) {
        state_ = IOState::Normal;
    }
    LogI("[IOManager] offset %lld, ,io state %d", offset_, static_cast<int>(state));
    if (callBack_) {
        callBack_(std::move(data), offset, state);
    }
}

bool IOManager::nextTask() noexcept {
    std::unique_lock<std::mutex> lock(mutex_);
    if (index_ != kInvalid && handler_) {
        handler_->close();
    }
    index_++;
    if (!CheckIndexValid(index_, paths_) || !handler_) {
        return false;
    }
    handler_->open(paths_[static_cast<size_t>(index_)]);
    return true;
}

void IOManager::updateEvent(TransportEvent event) {
    switch (event) {
        case TransportEvent::Start:
            open();
            break;
        case TransportEvent::Pause:
            pause();
            break;
        case TransportEvent::Stop:
            close();
            break;
        case TransportEvent::Reset:
            open();
            break;
        default:
            break;
    }
}

}//end namespace slark
