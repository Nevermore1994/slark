//
//  slark.mm
//  slark
//
//  Created by Nevermore

#import "slark.h"
#import "iOSUtil.h"
#include "Player.h"

#define STATE_ENUM_TO_CASE(value) \
    case slark::PlayerState::value: \
        return PlayerState##value

#define EVENT_ENUM_TO_CASE(value) \
    case slark::PlayerEvent::value: \
        return PlayerEvent##value

PlayerState convertState(slark::PlayerState state) {
    switch (state) {
            STATE_ENUM_TO_CASE(Initializing);
            STATE_ENUM_TO_CASE(Ready);
            STATE_ENUM_TO_CASE(Buffering);
            STATE_ENUM_TO_CASE(Playing);
            STATE_ENUM_TO_CASE(Pause);
            STATE_ENUM_TO_CASE(Stop);
            STATE_ENUM_TO_CASE(Error);
            STATE_ENUM_TO_CASE(Completed);
        default:
            return PlayerStateUnknown;
    }
}

PlayerEvent convertEvent(slark::PlayerEvent event) {
    switch (event) {
            EVENT_ENUM_TO_CASE(FirstFrameRendered);
            EVENT_ENUM_TO_CASE(SeekDone);
            EVENT_ENUM_TO_CASE(PlayEnd);
            EVENT_ENUM_TO_CASE(OnError);
    }
}

struct PlayerObserver final : public slark::IPlayerObserver
{
    void notifyTime(std::string_view playerId, long double time) override {
        if (notifyTimeFunc) {
            notifyTimeFunc(stringViewToNSString(playerId), time);
        }
    }

    void notifyState(std::string_view playerId, slark::PlayerState state) override {
        if (notifyStateFunc) {
            notifyStateFunc(stringViewToNSString(playerId), convertState(state));
        }
    }

    void event(std::string_view playerId, slark::PlayerEvent event, std::string value) override {
        if (notifyEventFunc) {
            notifyEventFunc(stringViewToNSString(playerId), convertEvent(event), stringViewToNSString(value));
        }
    }
    
    ~PlayerObserver() override = default;

    std::function<void(NSString*, long double)> notifyTimeFunc;
    std::function<void(NSString*, PlayerState)> notifyStateFunc;
    std::function<void(NSString*, PlayerEvent, NSString*)> notifyEventFunc;
};

@interface Player()
{
    std::unique_ptr<slark::Player> player_;
    std::unique_ptr<PlayerObserver> Observer_;
}

@end

@implementation Player

- (instancetype)Player:(NSString*) path {

}

- (void)play {
    
}

- (void)pause {
    
}

- (void)stop {
    
}
 
- (void)seek:(double) seekToTime {
    
}

- (void)seek:(double) seekToTime isAccurate:(BOOL)isAccurate {
    
}

- (void)setLoop:(BOOL) isLoop {
    
}

- (void)setVolume:(float) volume {
    
}

- (void)setMute:(BOOL) isMute {
    
}

- (void)setRenderSize:(int) width height:(int) height {
    
}

- (CMTime)totalDuration {
    
}

- (CMTime)currentTime {
    
}

- (PlayerState)state {
    
}

@end
