
//
// Copyright (c) 2022-2026 Nevermore1994. All rights reserved.
//
// This file is part of Slark.
// Licensed under the MIT License.
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

    tabBar.viewControllers = @[networkNav, localNav];
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
                     resultHandler:^(AVAsset* avAsset, AVAudioMix*, NSDictionary*) {
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
                                 }
                             });
                         }];
                     }];
    };
    return [[UINavigationController alloc] initWithRootViewController:pickerVC];
}

@end

#define SLARK_DEMO_CELL_ID @"slarkDemoCellId"

@implementation ViewController

- (void)viewDidLoad {
    [super viewDidLoad];
    [self initViews];
    [self initConfig];
    NSString* slarkTempDir = [NSTemporaryDirectory() stringByAppendingPathComponent:@"slark"];
    NSError* error;
    [[NSFileManager defaultManager] removeItemAtPath:slarkTempDir error:&error];
    NSLog(@"viewDidLoad");
}

- (void)viewWillAppear:(BOOL)animated {
    [super viewWillAppear:animated];
    [self.tabView reloadData];
}

- (void)initViews {
    self.navigationItem.title = @"slark demo";
    [self.navigationController.navigationBar
        setTitleTextAttributes:@{NSForegroundColorAttributeName: [UIColor blackColor]}];
    self.view.backgroundColor = [UIColor whiteColor];
    [self.view addSubview:self.tabView];
    [self.tabView mas_makeConstraints:^(MASConstraintMaker* make) {
        make.left.mas_equalTo(0);
        make.width.mas_equalTo(self.view.frame.size.width);
        make.top.mas_equalTo(NavigationHeight);
        make.bottom.mas_equalTo(-SafeBottomHeight);
    }];
}

- (void)initConfig {
    self.tabDatas =
        [NSArray arrayWithObjects:@"audio player", @"video player", @"network player", nil];
    [self.tabView reloadData];
}

- (UITableView*)tabView {
    if (_tabView == nil) {
        _tabView = [[UITableView alloc] initWithFrame:CGRectZero style:UITableViewStylePlain];
        [_tabView registerClass:UITableViewCell.class forCellReuseIdentifier:SLARK_DEMO_CELL_ID];
        _tabView.dataSource = self;
        _tabView.delegate = self;
        _tabView.backgroundColor = [UIColor whiteColor];
    }
    return _tabView;
}

#pragma mark - UITableViewDelegate

- (void)tableView:(UITableView*)tableView didSelectRowAtIndexPath:(NSIndexPath*)indexPath {
    switch (indexPath.row) {
        case 0: {
            AudioViewController* vc = [AudioViewController new];
            [self.navigationController pushViewController:vc animated:YES];
        } break;
        case 1: {
            VideoPickerViewController* vc =
                [[VideoPickerViewController alloc] initWithMode:PickerModeSingle];
            @weakify(self);
            vc.onResult = ^(NSArray<PHAsset*>* selectedAssets) {
                @strongify(self);
                PHAsset* phAsset = selectedAssets.firstObject;
                NSString* tempDir =
                    [NSTemporaryDirectory() stringByAppendingPathComponent:@"slark"];
                [[NSFileManager defaultManager] createDirectoryAtPath:tempDir
                                          withIntermediateDirectories:YES
                                                           attributes:nil
                                                                error:nil];
                NSString* fileName =
                    [NSString stringWithFormat:@"%@.mp4", [[NSUUID UUID] UUIDString]];
                NSString* destPath = [tempDir stringByAppendingPathComponent:fileName];
                NSURL* destURL = [NSURL fileURLWithPath:destPath];

                PHVideoRequestOptions* options = [[PHVideoRequestOptions alloc] init];
                options.version = PHVideoRequestOptionsVersionCurrent;
                [[PHImageManager defaultManager]
                    requestAVAssetForVideo:phAsset
                                   options:options
                             resultHandler:^(AVAsset* avAsset,
                                             AVAudioMix* /*audioMix*/,
                                             NSDictionary* /*info*/) {
                                 AVAssetExportSession* exportSession = [[AVAssetExportSession alloc]
                                     initWithAsset:avAsset
                                        presetName:AVAssetExportPresetPassthrough];
                                 exportSession.outputURL = destURL;
                                 exportSession.outputFileType = AVFileTypeMPEG4;
                                 exportSession.shouldOptimizeForNetworkUse = YES;

                                 [exportSession exportAsynchronouslyWithCompletionHandler:^{
                                     dispatch_async(dispatch_get_main_queue(), ^{
                                         if (exportSession.status ==
                                             AVAssetExportSessionStatusCompleted) {
                                             VideoViewController* videoVC =
                                                 [VideoViewController new];
                                             videoVC.path = [destURL path];
                                             [self.navigationController pushViewController:videoVC
                                                                                  animated:YES];
                                         } else {
                                             NSLog(@"error: %@", exportSession.error);
                                         }
                                     });
                                 }];
                             }];
            };
            [self.navigationController pushViewController:vc animated:YES];
        } break;
        case 2: {
            SelectUrlViewController* selectVC = [SelectUrlViewController new];
            selectVC.onItemClick = ^(NSString* url) {
                VideoViewController* videoVC = [VideoViewController new];
                videoVC.path = url;
                [self.navigationController pushViewController:videoVC animated:YES];
            };
            [self.navigationController pushViewController:selectVC animated:YES];
        } break;
        default:
            break;
    }
}

#pragma mark - UITableViewDataSource

- (NSInteger)tableView:(UITableView*)tableView numberOfRowsInSection:(NSInteger)section {
    return static_cast<NSInteger>(self.tabDatas.count);
}

- (UITableViewCell*)tableView:(UITableView*)tableView
        cellForRowAtIndexPath:(NSIndexPath*)indexPath {
    UITableViewCell* cell = [tableView dequeueReusableCellWithIdentifier:SLARK_DEMO_CELL_ID
                                                            forIndexPath:indexPath];
    auto index = static_cast<NSUInteger>(indexPath.row);
    if (index < self.tabDatas.count) {
        cell.textLabel.text = [self.tabDatas objectAtIndex:index];
        cell.textLabel.textAlignment = NSTextAlignment::NSTextAlignmentCenter;
        cell.textLabel.textColor = [UIColor blackColor];
        cell.backgroundColor = [UIColor clearColor];
    }
    return cell;
}

@end
