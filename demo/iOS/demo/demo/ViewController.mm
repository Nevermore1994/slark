//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#import "ViewController.h"

#import "EXTScope.h"
#import "Masonry.h"
#import "ViewController/AudioViewController.h"
#import "ViewController/SelectUrlViewController.h"
#import "ViewController/VideoPickerViewController.h"
#import "ViewController/VideoViewController.h"
#import "iOSUtil.h"

#import <AVFoundation/AVFoundation.h>
#import <Photos/Photos.h>

@implementation ViewController

- (void)viewDidLoad {
    [super viewDidLoad];
    [self setupTabBar];
    NSString* slarkTempDir = [NSTemporaryDirectory() stringByAppendingPathComponent:@"slark"];
    [[NSFileManager defaultManager] removeItemAtPath:slarkTempDir error:nil];
}

- (void)setupTabBar {
    UITabBarController* tabBar = [[UITabBarController alloc] init];

    UINavigationController* networkNav = [self makeNetworkNav];
    networkNav.tabBarItem =
        [[UITabBarItem alloc] initWithTitle:@"Network"
                                      image:[UIImage systemImageNamed:@"network"]
                              selectedImage:[UIImage systemImageNamed:@"network"]];

    UINavigationController* localNav = [self makeLocalNav];
    localNav.tabBarItem =
        [[UITabBarItem alloc] initWithTitle:@"Local"
                                      image:[UIImage systemImageNamed:@"folder"]
                              selectedImage:[UIImage systemImageNamed:@"folder.fill"]];

    UINavigationController* audioNav = [self makeAudioNav];
    audioNav.tabBarItem =
        [[UITabBarItem alloc] initWithTitle:@"Audio"
                                      image:[UIImage systemImageNamed:@"music.note"]
                              selectedImage:[UIImage systemImageNamed:@"music.note"]];

    tabBar.viewControllers = @[networkNav, localNav, audioNav];
    tabBar.tabBar.tintColor = [UIColor systemBlueColor];

    [self addChildViewController:tabBar];
    [self.view addSubview:tabBar.view];
    [tabBar.view mas_makeConstraints:^(MASConstraintMaker* make) {
        make.edges.equalTo(self.view);
    }];
    [tabBar didMoveToParentViewController:self];
}

- (UINavigationController*)makeNetworkNav {
    SelectUrlViewController* selectVC = [SelectUrlViewController new];
    @weakify(self);
    selectVC.onItemClick = ^(NSString* url) {
        @strongify(self);
        UINavigationController* nav =
            (UINavigationController*)((UITabBarController*)self.childViewControllers.firstObject)
                .selectedViewController;
        VideoViewController* videoVC = [VideoViewController new];
        videoVC.path = url;
        [nav pushViewController:videoVC animated:YES];
    };
    return [[UINavigationController alloc] initWithRootViewController:selectVC];
}

- (UINavigationController*)makeLocalNav {
    VideoPickerViewController* pickerVC =
        [[VideoPickerViewController alloc] initWithMode:PickerModeSingle];
    @weakify(self);
    pickerVC.onResult = ^(NSArray<PHAsset*>* selectedAssets) {
        @strongify(self);
        PHAsset* phAsset = selectedAssets.firstObject;
        NSString* tempDir = [NSTemporaryDirectory() stringByAppendingPathComponent:@"slark"];
        [[NSFileManager defaultManager] createDirectoryAtPath:tempDir
                                  withIntermediateDirectories:YES
                                                   attributes:nil
                                                        error:nil];
        NSString* destPath = [tempDir
            stringByAppendingPathComponent:[NSString stringWithFormat:@"%@.mp4",
                                                                      [[NSUUID UUID] UUIDString]]];
        NSURL* destURL = [NSURL fileURLWithPath:destPath];

        PHVideoRequestOptions* options = [[PHVideoRequestOptions alloc] init];
        options.version = PHVideoRequestOptionsVersionCurrent;
        [[PHImageManager defaultManager]
            requestAVAssetForVideo:phAsset
                           options:options
                     resultHandler:^(AVAsset* avAsset,
                                     AVAudioMix* /*audioMix*/,
                                     NSDictionary* /*info*/) {
                         AVAssetExportSession* session = [[AVAssetExportSession alloc]
                             initWithAsset:avAsset
                                presetName:AVAssetExportPresetPassthrough];
                         session.outputURL = destURL;
                         session.outputFileType = AVFileTypeMPEG4;
                         session.shouldOptimizeForNetworkUse = YES;
                         [session exportAsynchronouslyWithCompletionHandler:^{
                             dispatch_async(dispatch_get_main_queue(), ^{
                                 if (session.status == AVAssetExportSessionStatusCompleted) {
                                     UINavigationController* nav =
                                         (UINavigationController*)((UITabBarController*)
                                                                       self.childViewControllers
                                                                           .firstObject)
                                             .selectedViewController;
                                     VideoViewController* videoVC = [VideoViewController new];
                                     videoVC.path = [destURL path];
                                     [nav pushViewController:videoVC animated:YES];
                                 } else {
                                     NSLog(@"export error: %@", session.error);
                                 }
                             });
                         }];
                     }];
    };
    return [[UINavigationController alloc] initWithRootViewController:pickerVC];
}

- (UINavigationController*)makeAudioNav {
    AudioViewController* audioVC = [AudioViewController new];
    return [[UINavigationController alloc] initWithRootViewController:audioVC];
}

@end
