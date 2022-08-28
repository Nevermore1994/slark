//
// Created by Nevermore on 2021/10/25.
// slark ThreadManager
// Copyright (c) 2021 Nevermore All rights reserved.
//
#include "ThreadManager.hpp"
#include "Log.hpp"
#include "TimerManager.hpp"

namespace slark {
using namespace slark::Util;
using namespace slark::Time;
using namespace std::chrono_literals;


ThreadManager::ThreadManager() {
    timerId_ = TimerManager::shareInstance().runLoop(10s, [this]() {
        reportRunInfo();
    });
}

ThreadManager::~ThreadManager() {
    {
        std::unique_lock<std::mutex> lock(mutex_);
        threadInfos_.clear();
    }
    TimerManager::shareInstance().cancel(timerId_);
}


void ThreadManager::add(std::shared_ptr<Thread> thread) {
    std::unique_lock<std::mutex> lock(mutex_);
    threadInfos_.emplace(std::make_pair(thread->getId(), std::move(thread)));
}

void ThreadManager::remove(const std::shared_ptr<Thread>& thread) {
    std::unique_lock<std::mutex> lock(mutex_);
    threadInfos_.erase(thread->getId());
}

void ThreadManager::remove(std::thread::id id) {
    std::unique_lock<std::mutex> lock(mutex_);
    threadInfos_.erase(id);
}

Thread& ThreadManager::thisThread() {
    std::unique_lock<std::mutex> lock(mutex_);
    auto id = std::this_thread::get_id();
    if(threadInfos_.count(id)) {
        auto thread = threadInfos_[id];
        if(thread.expired()) {
            throw std::runtime_error("this thread is expired.");
        }
        return *thread.lock();
    } else {
        throw std::runtime_error("this thread not belong to ThreadManager.");
    }
}


void ThreadManager::reportRunInfo() noexcept {
    Timestamp now = nowTimeStamp();
    logi("ThreadManager report now:%llu, now live size :%lu", now, threadInfos_.size());
    //todo replace C++20 std::erase_if
    std::vector<std::thread::id> expiredThreads;
    std::vector<std::pair<std::thread::id, std::weak_ptr<Thread>>> threadInfos;
    {
        std::unique_lock<std::mutex> lock(mutex_);
        for(const auto& pair:threadInfos_){
            threadInfos.emplace_back(pair.first, pair.second);
        }
    }

    for(auto& pair:threadInfos) {
        auto t = pair.second;
        if(t.expired()) {
            expiredThreads.push_back(pair.first);
            continue;
        }
        auto thread = t.lock();
        Timestamp interval = (now - thread->getLastRunTimeStamp());
        if(thread->isRunning() && interval >= kMaxThreadBlockTimeInterval) {
            loge("ThreadManager report [%s] is blocking.", thread->getName().data());
        }
    }
    if(!expiredThreads.empty()){
        std::unique_lock<std::mutex> lock(mutex_);
        for(auto key: expiredThreads) {
            threadInfos_.erase(key);
        }
    }
}

}//end namespace slark
