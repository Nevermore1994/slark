//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#import "SelectUrlViewController.h"

@interface VideoItem : NSObject
@property (nonatomic, copy) NSString* name;
@property (nonatomic, copy) NSString* url;
+ (instancetype)initWithName:(NSString*)name url:(NSString*)url;
@end

@implementation VideoItem

+ (instancetype)initWithName:(NSString*)name url:(NSString*)url {
    VideoItem* item = [VideoItem new];
    item.name = name;
    item.url = url;
    return item;
}

@end

@interface SelectUrlViewController ()
@property (nonatomic, strong) UITextField* urlTextField;
@property (nonatomic, strong) UIButton* playButton;
@property (nonatomic, strong) UITableView* tableView;
@property (nonatomic, strong) NSArray<VideoItem*>* items;
@end

@implementation SelectUrlViewController

- (void)viewDidLoad {
    [super viewDidLoad];
    self.title = @"Network Player";
    self.view.backgroundColor = [UIColor systemBackgroundColor];

    self.items = @[
        [VideoItem initWithName:@"Sintel trailer mp4"
                            url:@"https:media.w3.org/2010/05/sintel/trailer.mp4"],
        [VideoItem initWithName:@"西瓜视频 mp4"
                            url:@"https:sf1-cdn-tos.huoshanstatic.com/obj/media-fe/"
                                @"xgplayer_doc_video/mp4/xgplayer-demo-360p.mp4"],
        [VideoItem initWithName:@"大灰熊 mp4" url:@"https:www.w3schools.com/html/movie.mp4"],
        [VideoItem initWithName:@"新闻 mp4"
                            url:@"https:stream7.iqilu.com/10339/upload_transcode/202002/09/"
                                @"20200209105011F0zPoYzHry.mp4"],
        [VideoItem initWithName:@"西瓜视频 m3u8"
                            url:@"https:sf1-cdn-tos.huoshanstatic.com/obj/media-fe/"
                                @"xgplayer_doc_video/hls/xgplayer-demo.m3u8"],
        [VideoItem
            initWithName:@"定时器 m3u8"
                     url:@"http:devimages.apple.com/iphone/samples/bipbop/gear3/prog_index.m3u8"],
        [VideoItem initWithName:@"大兔子 m3u8"
                            url:@"https:test-streams.mux.dev/x36xhzz/x36xhzz.m3u8"],
        [VideoItem initWithName:@"直播 m3u8"
                            url:@"http:kbs-dokdo.gscdn.com/dokdo_300/_definst_/dokdo_300.stream/"
                                @"playlist.m3u8"],
    ];

    [self setupInputArea];
    [self setupTableView];
}

- (void)setupInputArea {
    UIView* inputContainer = [[UIView alloc] init];
    inputContainer.backgroundColor = [UIColor secondarySystemBackgroundColor];
    inputContainer.layer.cornerRadius = 10;
    inputContainer.layer.masksToBounds = YES;
    [self.view addSubview:inputContainer];
    [inputContainer mas_makeConstraints:^(MASConstraintMaker* make) {
        make.top.mas_equalTo(NavigationHeight + 12);
        make.left.mas_equalTo(16);
        make.right.mas_equalTo(-16);
        make.height.mas_equalTo(44);
    }];

    self.urlTextField = [[UITextField alloc] init];
    self.urlTextField.placeholder = @"Enter URL (http/https)";
    self.urlTextField.font = [UIFont systemFontOfSize:14];
    self.urlTextField.clearButtonMode = UITextFieldViewModeWhileEditing;
    self.urlTextField.autocorrectionType = UITextAutocorrectionTypeNo;
    self.urlTextField.autocapitalizationType = UITextAutocapitalizationTypeNone;
    self.urlTextField.keyboardType = UIKeyboardTypeURL;
    self.urlTextField.returnKeyType = UIReturnKeyGo;
    self.urlTextField.delegate = (id<UITextFieldDelegate>)self;
    [inputContainer addSubview:self.urlTextField];
    [self.urlTextField mas_makeConstraints:^(MASConstraintMaker* make) {
        make.left.mas_equalTo(12);
        make.right.mas_equalTo(self.playButton.mas_left).offset(-8);
        make.centerY.equalTo(inputContainer);
    }];

    self.playButton = [UIButton buttonWithType:UIButtonTypeSystem];
    [self.playButton setImage:[UIImage systemImageNamed:@"play.circle.fill"]
                     forState:UIControlStateNormal];
    self.playButton.tintColor = [UIColor systemBlueColor];
    [self.playButton addTarget:self
                        action:@selector(playCustomUrl)
              forControlEvents:UIControlEventTouchUpInside];
    [inputContainer addSubview:self.playButton];
    [self.playButton mas_makeConstraints:^(MASConstraintMaker* make) {
        make.width.height.mas_equalTo(32);
        make.right.mas_equalTo(-8);
        make.centerY.equalTo(inputContainer);
    }];
}

- (void)setupTableView {
    UILabel* sectionLabel = [[UILabel alloc] init];
    sectionLabel.text = @"Presets";
    sectionLabel.font = [UIFont systemFontOfSize:13 weight:UIFontWeightMedium];
    sectionLabel.textColor = [UIColor secondaryLabelColor];
    [self.view addSubview:sectionLabel];
    [sectionLabel mas_makeConstraints:^(MASConstraintMaker* make) {
        make.top.mas_equalTo(NavigationHeight + 68);
        make.left.mas_equalTo(20);
    }];

    self.tableView = [[UITableView alloc] initWithFrame:CGRectZero style:UITableViewStylePlain];
    self.tableView.delegate = self;
    self.tableView.dataSource = self;
    self.tableView.backgroundColor = [UIColor systemBackgroundColor];
    [self.view addSubview:self.tableView];
    [self.tableView mas_makeConstraints:^(MASConstraintMaker* make) {
        make.top.mas_equalTo(NavigationHeight + 90);
        make.left.right.mas_equalTo(0);
        make.bottom.mas_equalTo(-SafeBottomHeight);
    }];
}

- (void)playCustomUrl {
    NSString* url = [self.urlTextField.text
        stringByTrimmingCharactersInSet:[NSCharacterSet whitespaceAndNewlineCharacterSet]];
    if (url.length == 0) {
        return;
    }
    [self.urlTextField resignFirstResponder];
    if (self.onItemClick) {
        self.onItemClick(url);
    }
}

- (BOOL)textFieldShouldReturn:(UITextField*)textField {
    [self playCustomUrl];
    return YES;
}

#pragma mark - UITableViewDataSource

- (NSInteger)tableView:(UITableView*)tableView numberOfRowsInSection:(NSInteger)section {
    return self.items.count;
}

- (UITableViewCell*)tableView:(UITableView*)tableView
        cellForRowAtIndexPath:(NSIndexPath*)indexPath {
    static NSString* cellId = @"UrlCell";
    UITableViewCell* cell = [tableView dequeueReusableCellWithIdentifier:cellId];
    if (!cell) {
        cell = [[UITableViewCell alloc] initWithStyle:UITableViewCellStyleSubtitle
                                      reuseIdentifier:cellId];
        cell.accessoryType = UITableViewCellAccessoryDisclosureIndicator;
    }
    VideoItem* item = self.items[indexPath.row];
    cell.textLabel.text = item.name;
    cell.detailTextLabel.text = item.url;
    cell.detailTextLabel.textColor = [UIColor secondaryLabelColor];
    return cell;
}

#pragma mark - UITableViewDelegate

- (void)tableView:(UITableView*)tableView didSelectRowAtIndexPath:(NSIndexPath*)indexPath {
    [tableView deselectRowAtIndexPath:indexPath animated:YES];
    if (self.onItemClick) {
        self.onItemClick(self.items[indexPath.row].url);
    }
}

@end
