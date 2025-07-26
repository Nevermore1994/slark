//
//  SlarkViewController.m
//  slark
//
//  Created by Nevermore on 2024/11/6.
//

#import <Foundation/Foundation.h>
#import "SlarkViewController.h"
#import "RenderGLView.h"
#import "GLContextManager.h"
#import "Player.h"

@interface SlarkPlayer()
- (std::unique_ptr<slark::Player>&) player;
@end

@interface SlarkPlayer(Render)
- (void)setRenderImpl:(std::weak_ptr<slark::IVideoRender>) ptr;

@end

@implementation SlarkPlayer(Render)

- (void)setRenderImpl:(std::weak_ptr<slark::IVideoRender>) ptr {
    auto& player = [self player];
    if (player) {
        player->setRenderImpl(ptr);
    }
}
@end

@interface SlarkViewController() <RenderViewDelegate>
@property(nonatomic, strong) RenderGLView* renderView;
@property(nonatomic, strong) SlarkPlayer* player;
@end

@implementation SlarkViewController

- (instancetype)initWithPlayer:(CGRect) frame player:(SlarkPlayer*) player {
    if (self = [self init]) {
        self.player = player;
        self.renderView = [[RenderGLView alloc] initWithFrame:frame];
        self.renderView.delegate = self;
        NSString* playerId = [self.player playerId];
        auto shareContext = slark::GLContextManager::shareInstance().createShareContextWithId([playerId UTF8String]);
        [self.renderView setContext:shareContext];
        [self.player setRenderImpl:[self.renderView renderImpl]];
        self.view = self.renderView;
    }
    return self;
}

- (void)setupNotifications{
    [[NSNotificationCenter defaultCenter] addObserver:self
                                             selector:@selector(deviceOrientationDidChange:)
                                                 name:UIDeviceOrientationDidChangeNotification
                                               object:nil];
    [[NSNotificationCenter defaultCenter] addObserver:self
                                             selector:@selector(applicationWillResignActive)
                                                 name:UIApplicationWillResignActiveNotification
                                               object:nil];
    [[NSNotificationCenter defaultCenter] addObserver:self
                                             selector:@selector(applicationDidBecomeActive)
                                                 name:UIApplicationDidBecomeActiveNotification
                                               object:nil];

}

- (void)viewDidLayoutSubviews {
    [super viewDidLayoutSubviews];
}

- (void)deviceOrientationDidChange:(NSNotification *)notification {
    UIDeviceOrientation orientation = [UIDevice currentDevice].orientation;
    if (orientation == UIDeviceOrientationPortrait || orientation == UIDeviceOrientationPortraitUpsideDown) {
    } else if (orientation == UIDeviceOrientationLandscapeLeft || orientation == UIDeviceOrientationLandscapeRight) {
    }
}

- (void)applicationWillResignActive {
    self.renderView.isActive = NO;
}

- (void)applicationDidBecomeActive {
    self.renderView.isActive = YES;
}

- (void)viewWillAppear:(BOOL)animated {
    [super viewWillAppear:animated];
    [self.renderView start];
}

- (void)viewWillDisappear:(BOOL)animated {
    [super viewWillDisappear:animated];
    [self.renderView stop];
}

- (void)dealloc {
    [self.player stop];
    [self.renderView stop];
}

@end
