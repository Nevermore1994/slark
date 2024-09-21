//
//  PlayerControllerView.h
//  demo
//

#import <UIKit/UIKit.h>
#ifndef PlayerControlView_h
#define PlayerControlView_h

@interface PlayerControllerView : UIView

@property(nonatomic, copy) void(^onPlayClick)(BOOL);
@property(nonatomic, copy) void(^onSeekClick)(double);
@property(nonatomic, copy) void(^onPrevClick)(void);
@property(nonatomic, copy) void(^onNextClick)(void);
@property(nonatomic, copy) void(^onSetLoopClick)(BOOL);

- (void)setIsPause:(BOOL) pause;

- (void)updateCurrentTime:(NSTimeInterval) value;

- (void)updateTotalTime:(NSTimeInterval) value;
@end

#endif /* PlayerControlViewController_h */
