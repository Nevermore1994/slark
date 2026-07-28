//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//
#ifndef SlarkViewController_h
#define SlarkViewController_h
#import <UIKit/UIKit.h>

#import "SlarkGraphicsConfig.h"
#import "SlarkPlayer.h"

@interface SlarkViewController : UIViewController
@property(nonatomic, strong, readonly) SlarkPlayer* player;
- (instancetype)initWithPlayer:(CGRect)frame player:(SlarkPlayer*)player;
/// Switch graphics backend (OpenGL ES / Metal) without interrupting playback.
- (void)switchGraphicsBackend:(SlarkGraphicsBackend)backend;
/// Switch graphics backend with HDR option.
- (void)switchGraphicsBackend:(SlarkGraphicsBackend)backend hdrEnabled:(BOOL)hdrEnabled;
@end

#endif /* SlarkViewController_h */