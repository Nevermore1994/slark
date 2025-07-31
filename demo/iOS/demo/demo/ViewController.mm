//
//  ViewController.m
//  demo
//
//  Created by Nevermore on 2023/10/18.
//

#import <AVFoundation/AVFoundation.h>
#import <Photos/Photos.h>
#import "Masonry.h"
#import "ViewController.h"
#import "ViewController/AudioViewController.h"
#import "ViewController/VideoViewController.h"
#import "ViewController/VideoPickerViewController.h"
#import "ViewController/SelectUrlViewController.h"
#import "iOSUtil.h"
#import "EXTScope.h"

@interface ViewController ()<UITableViewDelegate, UITableViewDataSource>

@property (nonatomic, strong) UITableView* tabView;
@property (nonatomic, strong) NSArray<NSString*>* tabDatas;

@end

#define SLARK_DEMO_CELL_ID @"slarkDemoCellId"

@implementation ViewController

- (void)viewDidLoad {
    [super viewDidLoad];
    [self initViews];
    [self initConfig];
    NSString *tempDir = NSTemporaryDirectory();
    NSError* error;
    [[NSFileManager defaultManager] removeItemAtPath:tempDir error:&error];
    NSLog(@"viewDidLoad");
}

- (void)viewWillAppear:(BOOL)animated {
    [super viewWillAppear: animated];
    [self.tabView reloadData];
}

- (void)initViews {
    self.navigationItem.title = @"slark demo";
    [self.navigationController.navigationBar setTitleTextAttributes:@{NSForegroundColorAttributeName:[UIColor blackColor]}];
    self.view.backgroundColor = [UIColor whiteColor];
    [self.view addSubview:self.tabView];
    [self.tabView mas_makeConstraints:^(MASConstraintMaker *make) {
        make.left.mas_equalTo(0);
        make.width.mas_equalTo(self.view.frame.size.width);
        make.top.mas_equalTo(NavigationHeight);
        make.bottom.mas_equalTo(-SafeBottomHeight);
    }];
}

- (void)initConfig {
    self.tabDatas = [NSArray arrayWithObjects:
                     @"audio player",
                     @"video player",
                     @"network player",
                     nil];
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

- (void)tableView:(UITableView *)tableView didSelectRowAtIndexPath:(NSIndexPath *)indexPath {
    switch (indexPath.row) {
        case 0: {
            AudioViewController* vc = [AudioViewController new];
            [self.navigationController pushViewController:vc animated:YES];
        }
            break;
        case 1: {
            VideoPickerViewController* vc = [[VideoPickerViewController alloc] initWithMode:PickerModeSingle];
            @weakify(self);
            vc.onResult = ^(NSArray<PHAsset*>* selectedAssets) {
                @strongify(self);
                PHAsset *phAsset = selectedAssets.firstObject;
                NSString *tempDir = NSTemporaryDirectory();
                [[NSFileManager defaultManager] createDirectoryAtPath:tempDir withIntermediateDirectories:YES attributes:nil error:nil];
                NSString *fileName = [NSString stringWithFormat:@"%@.mp4", [[NSUUID UUID] UUIDString]];
                NSString *destPath = [tempDir stringByAppendingPathComponent:fileName];
                NSURL *destURL = [NSURL fileURLWithPath:destPath];

                PHVideoRequestOptions *options = [[PHVideoRequestOptions alloc] init];
                options.version = PHVideoRequestOptionsVersionCurrent;
                [[PHImageManager defaultManager] requestAVAssetForVideo:phAsset options:options resultHandler:^(AVAsset *avAsset, AVAudioMix* /*audioMix*/, NSDictionary* /*info*/) {
                    AVAssetExportSession *exportSession = [[AVAssetExportSession alloc] initWithAsset:avAsset presetName:AVAssetExportPresetPassthrough];
                    exportSession.outputURL = destURL;
                    exportSession.outputFileType = AVFileTypeMPEG4;
                    exportSession.shouldOptimizeForNetworkUse = YES;

                    [exportSession exportAsynchronouslyWithCompletionHandler:^{
                        dispatch_async(dispatch_get_main_queue(), ^{
                            if (exportSession.status == AVAssetExportSessionStatusCompleted) {
                                VideoViewController* videoVC = [VideoViewController new];
                                videoVC.path = [NSTemporaryDirectory() stringByAppendingPathComponent:destURL.lastPathComponent];
                                [self.navigationController pushViewController:videoVC animated:YES];
                            } else {
                                NSLog(@"error: %@", exportSession.error);
                            }
                        });
                    }];
                }];
            };
            [self.navigationController pushViewController:vc animated:YES];
        }
            break;
        case 3: {
            SelectUrlViewController* selectVC = [SelectUrlViewController new];
            selectVC.onItemClick = ^(NSString* url) {
                VideoViewController* videoVC = [VideoViewController new];
                videoVC.path = url;
                [self.navigationController pushViewController:videoVC animated:YES];
            };
            [self.navigationController pushViewController:selectVC animated:YES];
        }
            break;
        default:
            break;
    }
}

#pragma mark - UITableViewDataSource
- (NSInteger)tableView:(UITableView *)tableView numberOfRowsInSection:(NSInteger)section {
    return static_cast<NSInteger>(self.tabDatas.count);
}

- (UITableViewCell *)tableView:(UITableView *)tableView cellForRowAtIndexPath:(NSIndexPath *)indexPath {
    UITableViewCell* cell = [tableView dequeueReusableCellWithIdentifier:SLARK_DEMO_CELL_ID forIndexPath:indexPath];
    auto index = static_cast<NSUInteger>(indexPath.row);
    if(index < self.tabDatas.count) {
        cell.textLabel.text = [self.tabDatas objectAtIndex:index];
        cell.textLabel.textAlignment = NSTextAlignment::NSTextAlignmentCenter;
        cell.textLabel.textColor = [UIColor blackColor];
        cell.backgroundColor = [UIColor clearColor];
    }
    return cell;
}
@end
