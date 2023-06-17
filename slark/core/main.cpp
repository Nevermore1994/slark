//
//  main.cpp
//
//  Created by Nevermore on 2022/4/14.
//

#include <memory>
#include <iostream>
#include "Player.hpp"
#include "BaseClass.hpp"
#include "WavDemuxer.hpp"
#include "FileUtility.hpp"
#include "Log.hpp"
#include "Time.hpp"

using namespace Slark;

struct Observer:public IPlayerObserver, public std::enable_shared_from_this<Observer>{
    inline void updateTime(long double time) override{
        LogI("%f", time);
    }
    
    inline void updateState(PlayerState state) override{
        LogI("core:%d", static_cast<int>(state));
    }
    
    inline std::weak_ptr<Observer> observer() noexcept{
        return weak_from_this();
    }
    
    inline void event(PlayerEvent event) override {
    
    }
};



int main(int argc, const char * argv[]) {
    auto t = Time::nowTimeStamp();
    std::cout << "now time:" << Time::localTime() << std::endl;

//    if(FileUtil::isFileExist("test.wav")){
//        LogI("file is exist %lld", FileUtil::fileSize("test.wav"));
//    } else {
//        LogE("file not exist");
//    }
//    auto params = std::make_shared<PlayerParams>();
//    ResourceItem item;
//    item.path = "test.wav";
//    params->items.push_back(std::move(item));
//    Observer observer;
//    params->observer = observer.observer();
//    Player player(params);
//    player.play();
//    std::cout << player.playerId() << std::endl;
    return 0;
}