//
//  SlarkViewController.h
//  slark
//
//  Created by Nevermore on 2024/11/6.
//

#ifndef SlarkViewController_h
#define SlarkViewController_h
#import "SlarkGraphicsConfig.h"
#import "SlarkPlayer.h"

#import <UIKit/UIKit.h>

@interface SlarkViewController : UIViewController
@property (nonatomic, strong, readonly) SlarkPlayer* player;
- (instancetype)initWithPlayer:(CGRect)frame player:(SlarkPlayer*)player;
/// Switch graphics backend (OpenGL ES / Metal) without interrupting playback.
- (void)switchGraphicsBackend:(SlarkGraphicsBackend)backend;
/// Switch graphics backend with HDR option.
- (void)switchGraphicsBackend:(SlarkGraphicsBackend)backend hdrEnabled:(BOOL)hdrEnabled;
@end

#endif /* SlarkViewController_h */
