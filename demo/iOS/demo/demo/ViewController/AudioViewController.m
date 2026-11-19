
//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#import "AudioViewController.h"

#import "EXTScope.h"
#import "Masonry.h"
#import "PlayerControllerView.h"
#include "SlarkPlayer.h"

#import <AVFoundation/AVFoundation.h>
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>

static NSString* const kTrackCellId = @"audioTrackCell";
static NSString* const kBundledAudioName = @"sample-3s.wav";

@interface AudioViewController () <UIDocumentPickerDelegate,
                                   UITableViewDataSource,
                                   UITableViewDelegate,
                                   ISlarkPlayerObserver>
@property (nonatomic, strong) UIImageView* iconView;
@property (nonatomic, strong) PlayerControllerView* controllerView;
@property (nonatomic, strong) UITableView* tableView;
@property (nonatomic, strong) NSMutableArray<NSURL*>* tracks;
@property (nonatomic, strong) NSMutableArray<NSString*>* trackTitles;
@property (nonatomic, assign) NSInteger currentIndex;
@property (nonatomic, assign) BOOL loopEnabled;
@property (nonatomic, strong) SlarkPlayer* player;
@property (nonatomic, strong) UILabel* debugLabel;
@property (nonatomic, strong) NSTimer* debugTimer;
@end

@implementation AudioViewController

- (void)viewDidLoad {
    [super viewDidLoad];
    self.tracks = [NSMutableArray array];
    self.trackTitles = [NSMutableArray array];
    self.currentIndex = -1;
    [self initSubviews];
    [self loadBundledTrack];
}

- (void)dealloc {
    [self.debugTimer invalidate];
    self.debugTimer = nil;
    [self.player stop];
    self.player.delegate = nil;
    self.player = nil;
}

- (void)viewDidAppear:(BOOL)animated {
    [super viewDidAppear:animated];
    [self setupAudioSession];
}

- (void)setupAudioSession {
    AVAudioSession* session = [AVAudioSession sharedInstance];
    NSError* error = nil;
    [session setCategory:AVAudioSessionCategoryPlayback error:&error];
    [session setActive:YES error:&error];
}

- (void)initSubviews {
    self.view.backgroundColor = [UIColor blackColor];
    self.navigationItem.title = @"Audio";
    [self.navigationController.navigationBar
        setTitleTextAttributes:@{NSForegroundColorAttributeName: [UIColor whiteColor]}];

    UIBarButtonItem* addItem = [[UIBarButtonItem alloc] initWithBarButtonSystemItem:UIBarButtonSystemItemAdd
                                                                              target:self
                                                                              action:@selector(browseAudioFiles)];
    self.navigationItem.rightBarButtonItem = addItem;

    [self.view addSubview:self.iconView];
    [self.view addSubview:self.controllerView];
    [self.view addSubview:self.tableView];
    [self.view addSubview:self.debugLabel];

    [self.controllerView mas_makeConstraints:^(MASConstraintMaker* make) {
        make.left.right.mas_equalTo(self.view);
        make.bottom.equalTo(self.tableView.mas_top);
    }];
    [self.tableView mas_makeConstraints:^(MASConstraintMaker* make) {
        make.left.right.mas_equalTo(self.view);
        make.bottom.mas_equalTo(self.view.mas_safeAreaLayoutGuideBottom);
        make.height.mas_equalTo(180);
    }];
    [self.iconView mas_makeConstraints:^(MASConstraintMaker* make) {
        make.top.mas_equalTo(self.view.mas_safeAreaLayoutGuideTop).offset(16);
        make.left.right.mas_equalTo(self.view).insets(UIEdgeInsetsMake(0, 60, 0, 60));
        make.bottom.equalTo(self.controllerView.mas_top).offset(-16);
    }];
    [self.debugLabel mas_makeConstraints:^(MASConstraintMaker* make) {
        make.left.mas_equalTo(8);
        make.top.mas_equalTo(self.view.mas_safeAreaLayoutGuideTop).offset(8);
        make.width.mas_equalTo(300);
        make.height.mas_lessThanOrEqualTo(180);
    }];
    self.debugTimer = [NSTimer scheduledTimerWithTimeInterval:0.2
                                                      target:self
                                                    selector:@selector(refreshDebugInfo)
                                                    userInfo:nil
                                                     repeats:YES];
}

- (void)refreshDebugInfo {
    self.debugLabel.text = [NSString stringWithFormat:@"%@Backend: n/a\n",
                                                     [self.player debugSnapshot]];
}

- (UILabel*)debugLabel {
    if (_debugLabel == nil) {
        _debugLabel = [[UILabel alloc] init];
        _debugLabel.numberOfLines = 0;
        _debugLabel.textColor = [UIColor whiteColor];
        _debugLabel.font = [UIFont systemFontOfSize:9];
        _debugLabel.backgroundColor = [[UIColor blackColor] colorWithAlphaComponent:0.6];
        _debugLabel.layer.cornerRadius = 8;
        _debugLabel.clipsToBounds = YES;
    }
    return _debugLabel;
}

- (void)loadBundledTrack {
    NSString* bundlePath = [[NSBundle mainBundle] pathForResource:@"resource" ofType:@"bundle"];
    NSBundle* resourceBundle = [NSBundle bundleWithPath:bundlePath];
    NSURL* url = [resourceBundle URLForResource:kBundledAudioName withExtension:nil];
    if (url) {
        [self.tracks addObject:url];
        [self.trackTitles addObject:kBundledAudioName];
        [self.tableView reloadData];
        [self playTrackAtIndex:0];
    }
}

- (void)playTrackAtIndex:(NSInteger)index {
    if (index < 0 || index >= (NSInteger)self.tracks.count) {
        return;
    }
    self.currentIndex = index;
    [self.tableView reloadData];
    [self.tableView selectRowAtIndexPath:[NSIndexPath indexPathForRow:index inSection:0]
                                animated:NO
                          scrollPosition:UITableViewScrollPositionNone];

    [self.player stop];
    self.player.delegate = nil;
    self.player = nil;
    [self.controllerView syncPause:YES];
    [self.controllerView updateCurrentTime:0];
    [self.controllerView updateTotalTime:0];
    [self.controllerView updateCacheTime:0];

    self.navigationItem.title = self.trackTitles[index];
    self.player = [[SlarkPlayer alloc] init:self.tracks[index].path];
    self.player.delegate = self;
    self.player.isLoop = self.loopEnabled;
    [self.player prepare];
}

- (void)playPreviousTrack {
    if (self.tracks.count == 0) {
        return;
    }
    NSInteger previous = self.currentIndex > 0 ? self.currentIndex - 1 : (NSInteger)self.tracks.count - 1;
    [self playTrackAtIndex:previous];
}

- (void)playNextTrack {
    if (self.tracks.count == 0) {
        return;
    }
    NSInteger next = self.currentIndex < (NSInteger)self.tracks.count - 1 ? self.currentIndex + 1 : 0;
    [self playTrackAtIndex:next];
}

- (void)browseAudioFiles {
    UIDocumentPickerViewController* picker =
        [[UIDocumentPickerViewController alloc] initForOpeningContentTypes:@[UTTypeAudio]
                                                                    asCopy:YES];
    picker.delegate = self;
    [self presentViewController:picker animated:YES completion:nil];
}

- (void)documentPicker:(UIDocumentPickerViewController*)controller
    didPickDocumentsAtURLs:(NSArray<NSURL*>*)urls {
    NSURL* url = urls.firstObject;
    if (!url.isFileURL) {
        return;
    }
    [self.tracks addObject:url];
    [self.trackTitles addObject:url.lastPathComponent];
    [self.tableView reloadData];
    [self playTrackAtIndex:(NSInteger)self.tracks.count - 1];
}

- (void)handlePlayClick:(BOOL)isPlay {
    if (self.player == nil) {
        return;
    }
    if (isPlay) {
        [self.player play];
    } else {
        [self.player pause];
    }
}

- (UIImageView*)iconView {
    if (_iconView == nil) {
        _iconView = [[UIImageView alloc] init];
        _iconView.image = [UIImage imageNamed:@"music_normal"];
        _iconView.contentMode = UIViewContentModeScaleAspectFit;
    }
    return _iconView;
}

- (PlayerControllerView*)controllerView {
    if (_controllerView == nil) {
        _controllerView = [[PlayerControllerView alloc] initWithFrame:CGRectZero];
        @weakify(self);
        _controllerView.onPlayClick = ^(BOOL isPlay) {
            @strongify(self);
            [self handlePlayClick:isPlay];
        };
        _controllerView.onPrevClick = ^{
            @strongify(self);
            [self playPreviousTrack];
        };
        _controllerView.onNextClick = ^{
            @strongify(self);
            [self playNextTrack];
        };
        _controllerView.onSeekClick = ^(double time) {
            @strongify(self);
            [self.player seek:time];
        };
        _controllerView.onSeekDone = ^(double time) {
            @strongify(self);
            [self.player seek:time isAccurate:YES];
        };
        _controllerView.onSetLoopClick = ^(BOOL loop) {
            @strongify(self);
            self.loopEnabled = loop;
            self.player.isLoop = loop;
        };
        _controllerView.onSetMute = ^(BOOL isMute) {
            @strongify(self);
            self.player.isMute = isMute;
        };
        _controllerView.onVolumeChange = ^(float volume) {
            @strongify(self);
            self.player.volume = volume;
        };
    }
    return _controllerView;
}

- (UITableView*)tableView {
    if (_tableView == nil) {
        _tableView = [[UITableView alloc] initWithFrame:CGRectZero style:UITableViewStylePlain];
        _tableView.backgroundColor = [UIColor blackColor];
        _tableView.separatorColor = [[UIColor whiteColor] colorWithAlphaComponent:0.2];
        _tableView.dataSource = self;
        _tableView.delegate = self;
        [_tableView registerClass:UITableViewCell.class forCellReuseIdentifier:kTrackCellId];
    }
    return _tableView;
}

#pragma mark - UITableViewDataSource

- (NSInteger)tableView:(UITableView*)tableView numberOfRowsInSection:(NSInteger)section {
    return (NSInteger)self.trackTitles.count;
}

- (UITableViewCell*)tableView:(UITableView*)tableView cellForRowAtIndexPath:(NSIndexPath*)indexPath {
    UITableViewCell* cell = [tableView dequeueReusableCellWithIdentifier:kTrackCellId
                                                            forIndexPath:indexPath];
    cell.backgroundColor = [UIColor blackColor];
    cell.textLabel.text = self.trackTitles[indexPath.row];
    cell.textLabel.textColor = indexPath.row == self.currentIndex
                                   ? [UIColor systemBlueColor]
                                   : [UIColor whiteColor];
    cell.textLabel.font = [UIFont systemFontOfSize:15];
    return cell;
}

#pragma mark - UITableViewDelegate

- (void)tableView:(UITableView*)tableView didSelectRowAtIndexPath:(NSIndexPath*)indexPath {
    [tableView deselectRowAtIndexPath:indexPath animated:YES];
    [self playTrackAtIndex:indexPath.row];
}

#pragma mark - ISlarkPlayerObserver

- (void)notifyTime:(NSString*)playerId time:(double)time {
    [self.controllerView updateCurrentTime:time];
}

- (void)notifyState:(NSString*)playerId state:(SlarkPlayerState)state {
    if (state == SlarkPlayerStatePrepared) {
        [self.controllerView updateTotalTime:CMTimeGetSeconds(self.player.totalDuration)];
        [self.controllerView setIsSeekable:self.player.isSeekable];
    } else if (state == SlarkPlayerStateCompleted || state == SlarkPlayerStatePause) {
        [self.controllerView syncPause:YES];
    } else if (state == SlarkPlayerStatePlaying) {
        [self.controllerView syncPause:NO];
    }
}

- (void)notifyEvent:(NSString*)playerId event:(SlarkPlayerEvent)event value:(NSString*)value {
    if (event == SlarkPlayerEventUpdateCacheTime) {
        [self.controllerView updateCacheTime:[value doubleValue]];
    }
}

@end
