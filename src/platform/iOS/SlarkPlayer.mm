//
//  SlarkPlayer.mm
//  slark
//
//  Created by Nevermore

#import "SlarkPlayer.h"
#import "iOSBase.h"
#import "Player.h"
#import "GLContextManager.h"
#import "RenderGLViewDelegate.h"

#define STATE_ENUM_TO_CASE(value) \
    case slark::PlayerState::value: \
        return SlarkPlayerState##value

#define EVENT_ENUM_TO_CASE(value) \
    case slark::PlayerEvent::value: \
        return SlarkPlayerEvent##value

SlarkPlayerState convertState(slark::PlayerState state) {
    switch (state) {
            STATE_ENUM_TO_CASE(NotInited);
            STATE_ENUM_TO_CASE(Initializing);
            STATE_ENUM_TO_CASE(Prepared);
            STATE_ENUM_TO_CASE(Buffering);
            STATE_ENUM_TO_CASE(Ready);
            STATE_ENUM_TO_CASE(Playing);
            STATE_ENUM_TO_CASE(Pause);
            STATE_ENUM_TO_CASE(Stop);
            STATE_ENUM_TO_CASE(Error);
            STATE_ENUM_TO_CASE(Completed);
        default:
            return SlarkPlayerStateUnknown;
    }
}

SlarkPlayerEvent convertEvent(slark::PlayerEvent event) {
    switch (event) {
            EVENT_ENUM_TO_CASE(FirstFrameRendered);
            EVENT_ENUM_TO_CASE(SeekDone);
            EVENT_ENUM_TO_CASE(PlayEnd);
            EVENT_ENUM_TO_CASE(UpdateCacheTime);
            EVENT_ENUM_TO_CASE(OnError);
    }
}

struct PlayerObserver final : public slark::IPlayerObserver
{
    void notifyPlayedTime(std::string_view playerId, double time) override {
        if (notifyTimeFunc) {
            notifyTimeFunc(stringViewToNSString(playerId), time);
        }
    }

    void notifyPlayerState(std::string_view playerId, slark::PlayerState state) override {
        if (notifyStateFunc) {
            notifyStateFunc(stringViewToNSString(playerId), convertState(state));
        }
    }

    void notifyPlayerEvent(std::string_view playerId, slark::PlayerEvent event, std::string value) override {
        if (notifyEventFunc) {
            notifyEventFunc(stringViewToNSString(playerId), convertEvent(event), stringViewToNSString(value));
        }
    }
    
    ~PlayerObserver() override = default;

    std::function<void(NSString*, long double)> notifyTimeFunc;
    std::function<void(NSString*, SlarkPlayerState)> notifyStateFunc;
    std::function<void(NSString*, SlarkPlayerEvent, NSString*)> notifyEventFunc;
};

@interface SlarkPlayer()
{
    std::unique_ptr<slark::Player> _player;
    std::shared_ptr<PlayerObserver> _observer;
}
@property (nonatomic, weak) id<RenderGLViewDelegate> renderDelegate;
@end

@implementation SlarkPlayer

- (instancetype)init:(NSString*) path {
    return [self initWithTimeRange:path range:kCMTimeRangeInvalid];
}

- (instancetype)initWithTimeRange:(NSString*) path range:(CMTimeRange)range {
    if (self = [super init]) {
        auto params = std::make_unique<slark::PlayerParams>();
        params->item.path = [path UTF8String];
        auto context = slark::createGLContext();
        context->init(nullptr);
        if (CMTIMERANGE_IS_VALID(range)) {
            params->item.displayStart = CMTimeGetSeconds(range.start);
            params->item.displayDuration = CMTimeGetSeconds(range.duration);
        }
        params->mainGLContext = std::shared_ptr<slark::IEGLContext>(std::move(context));
        _player = std::make_unique<slark::Player>(std::move(params));
        _observer = std::make_shared<PlayerObserver>();
        _player->addObserver(_observer);
        __weak __typeof(self) weakSelf = self;
        _observer->notifyTimeFunc = [weakSelf](NSString* playerId, long double time){
            dispatch_async(dispatch_get_main_queue(), ^{
                __strong __typeof(weakSelf) strongSelf = weakSelf;
                if ([strongSelf.delegate respondsToSelector:@selector(notifyTime:time:)]) {
                    [strongSelf.delegate notifyTime:playerId time:time];
                }
            });
        };
        _observer->notifyStateFunc = [weakSelf](NSString* playerId, SlarkPlayerState state){
            dispatch_async(dispatch_get_main_queue(), ^{
                __strong __typeof(weakSelf) strongSelf = weakSelf;
                if ([strongSelf.delegate respondsToSelector:@selector(notifyState:state:)]) {
                    [strongSelf.delegate notifyState:playerId state:state];
                }
            });
        };
        _observer->notifyEventFunc = [weakSelf](NSString* playerId, SlarkPlayerEvent evnt, NSString* value){
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

- (void)prepare {
    if (_player) {
        _player->prepare();
    }
}

- (void)play {
    if (_player) {
        _player->play();
    }
}

- (void)pause {
    if (_player) {
        _player->pause();
    }
}

- (void)stop {
    if (_player) {
        _player->stop();
    }
}
 
- (void)seek:(double) seekToTime {
    if (_player) {
        _player->seek(seekToTime);
    }
}

- (void)seek:(double) seekToTime isAccurate:(BOOL)isAccurate {
    if (_player) {
        _player->seek(seekToTime, static_cast<bool>(isAccurate));
    }
}

- (void)setLoop:(BOOL) isLoop {
    if (_player) {
        _player->setLoop(static_cast<bool>(isLoop));
    }
}

- (void)setVolume:(float) volume {
    if (_player) {
        _player->setVolume(volume);
    }
}

- (void)setMute:(BOOL) isMute {
    if (_player) {
        _player->setMute(static_cast<bool>(isMute));
    }
}

- (CMTime)totalDuration {
    if (_player) {
        return CMTimeMakeWithSeconds(_player->info().duration, 1000);
    }
    return kCMTimeZero;
}

- (CMTime)currentTime {
    if (_player) {
        return CMTimeMakeWithSeconds(_player->currentPlayedTime(), 1000);
    }
    return kCMTimeZero;
}

- (SlarkPlayerState)state {
    if (_player) {
        return convertState(_player->state());
    }
    return SlarkPlayerStateUnknown;
}

- (NSString*)playerId {
    if (_player) {
        return [NSString stringWithUTF8String:_player->playerId().data()];
    }
    return @"";
}

- (void)setRotation:(SlarkPlayerRotation)rotation {
    double t = 0;
    switch (rotation) {
        case SlarkPlayerRotation_0:
            t = 0;
            break;
        case SlarkPlayerRotation_90:
            t = 90;
            break;
        case SlarkPlayerRotation_180:
            t = 180;
            break;
        case SlarkPlayerRotation_270:
            t = 270;
            break;
        default:
            return;
    }
    if ([self.renderDelegate respondsToSelector:@selector(setRotation:)]) {
        [self.renderDelegate setRotation:t];
    }
}

- (void)setRenderImpl:(std::weak_ptr<slark::IVideoRender>) ptr {
    if (_player) {
        _player->setRenderImpl(ptr);
    }
}
@end
