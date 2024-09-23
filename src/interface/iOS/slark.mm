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

    void notifyEvent(std::string_view playerId, slark::PlayerEvent event, std::string value) override {
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
    std::shared_ptr<PlayerObserver> observer_;
}

@end

@implementation Player

- (instancetype)init:(NSString*) path {
    return [self initWithTimeRange:path range:kCMTimeRangeInvalid];
}

- (instancetype)initWithTimeRange:(NSString*) path range:(CMTimeRange)range {
    if (self = [super init]) {
        auto params = std::make_unique<slark::PlayerParams>();
        params->item.path = [path UTF8String];
        if (CMTIMERANGE_IS_VALID(range)) {
            params->item.displayStart = CMTimeGetSeconds(range.start);
            params->item.displayDuration = CMTimeGetSeconds(range.duration);
        }
        player_ = std::make_unique<slark::Player>(std::move(params));
        observer_ = std::make_shared<PlayerObserver>();
        player_->addObserver(observer_);
        __weak __typeof(self) weakSelf = self;
        observer_->notifyTimeFunc = [weakSelf](NSString* playerId, long double time){
            dispatch_async(dispatch_get_main_queue(), ^{
                __strong __typeof(weakSelf) strongSelf = weakSelf;
                if ([strongSelf.delegate respondsToSelector:@selector(notifyTime:time:)]) {
                    [strongSelf.delegate notifyTime:playerId time:time];
                }
            });
        };
        observer_->notifyStateFunc = [weakSelf](NSString* playerId, PlayerState state){
            dispatch_async(dispatch_get_main_queue(), ^{
                __strong __typeof(weakSelf) strongSelf = weakSelf;
                if ([strongSelf.delegate respondsToSelector:@selector(notifyState:state:)]) {
                    [strongSelf.delegate notifyState:playerId state:state];
                }
            });
        };
        observer_->notifyEventFunc = [weakSelf](NSString* playerId, PlayerEvent evnt, NSString* value){
            dispatch_async(dispatch_get_main_queue(), ^{
                __strong __typeof(weakSelf) strongSelf = weakSelf;
                if ([strongSelf.delegate respondsToSelector:@selector(notifyEvent:event:value:)]) {
                    [strongSelf.delegate notifyEvent:playerId event:evnt value:value];
                }
            });
        };
    }
    return self;
}

- (void)play {
    player_->play();
}

- (void)pause {
    player_->pause();
}

- (void)stop {
    player_->stop();
}
 
- (void)seek:(double) seekToTime {
    player_->seek(seekToTime);
}

- (void)seek:(double) seekToTime isAccurate:(BOOL)isAccurate {
    player_->seek(seekToTime, static_cast<bool>(isAccurate));
}

- (void)setLoop:(BOOL) isLoop {
    player_->setLoop(static_cast<bool>(isLoop));
}

- (void)setVolume:(float) volume {
    player_->setVolume(volume);
}

- (void)setMute:(BOOL) isMute {
    player_->setMute(static_cast<bool>(isMute));
}

- (void)setRenderSize:(int) width height:(int) height {
    player_->setRenderSize(static_cast<uint32_t>(width), static_cast<uint32_t>(height));
}

- (CMTime)totalDuration {
    return CMTimeMakeWithSeconds(player_->info().duration, 1000);
}

- (CMTime)currentTime {
    return CMTimeMakeWithSeconds(player_->currentPlayedTime(), 1000);
}

- (PlayerState)state {
    return convertState(player_->state());
}

@end
