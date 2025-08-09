//
//  SelectUrlViewController.m
//  demo
//
//  Created by Nevermore- on 2025/7/29.
//

#import "SelectUrlViewController.h"

@interface VideoItem : NSObject
@property (nonatomic, copy) NSString* name;
@property (nonatomic, copy) NSString* url;

+ (instancetype)initWithName:(NSString*) name url:(NSString*) url;
@end

@implementation VideoItem

+ (instancetype)initWithName:(NSString*) name url:(NSString*) url {
    VideoItem* item = [VideoItem new];
    item.name = name;
    item.url = url;
    return item;
}

@end

@interface SelectUrlViewController ()
@property (nonatomic, strong) UITableView *tableView;
@property (nonatomic, strong) NSArray<VideoItem*> *items;
@end

@implementation SelectUrlViewController

- (void)viewDidLoad {
    [super viewDidLoad];
    self.title = @"Select Video URL";
    self.view.backgroundColor = [UIColor whiteColor];

    self.items = @[
        [VideoItem initWithName:@"Sintel trailer mp4" url:@"https://media.w3.org/2010/05/sintel/trailer.mp4"],
        [VideoItem initWithName:@"西瓜视频 mp4" url:@"https://sf1-cdn-tos.huoshanstatic.com/obj/media-fe/xgplayer_doc_video/mp4/xgplayer-demo-360p.mp4"],
        [VideoItem initWithName:@"大灰熊 mp4" url:@"https://www.w3schools.com/html/movie.mp4"],
        [VideoItem initWithName:@"新闻 mp4" url:@"https://stream7.iqilu.com/10339/upload_transcode/202002/09/20200209105011F0zPoYzHry.mp4"],
        [VideoItem initWithName:@"西瓜视频 m3u8" url:@"https://sf1-cdn-tos.huoshanstatic.com/obj/media-fe/xgplayer_doc_video/hls/xgplayer-demo.m3u8"],
        [VideoItem initWithName:@"定时器 m3u8" url:@"http://devimages.apple.com/iphone/samples/bipbop/gear3/prog_index.m3u8"],
        [VideoItem initWithName:@"大兔子 m3u8" url:@"https://test-streams.mux.dev/x36xhzz/x36xhzz.m3u8"]
    ];

    self.tableView = [[UITableView alloc] initWithFrame:self.view.bounds style:UITableViewStylePlain];
    self.tableView.delegate = self;
    self.tableView.dataSource = self;
    self.tableView.autoresizingMask = UIViewAutoresizingFlexibleWidth | UIViewAutoresizingFlexibleHeight;
    [self.view addSubview:self.tableView];
}

#pragma mark - UITableViewDataSource

- (NSInteger)tableView:(UITableView *)tableView numberOfRowsInSection:(NSInteger)section {
    return self.items.count;
}

- (UITableViewCell *)tableView:(UITableView *)tableView cellForRowAtIndexPath:(NSIndexPath *)indexPath {
    static NSString *cellId = @"UrlCell";
    UITableViewCell *cell = [tableView dequeueReusableCellWithIdentifier:cellId];
    if (!cell) {
        cell = [[UITableViewCell alloc] initWithStyle:UITableViewCellStyleDefault reuseIdentifier:cellId];
        cell.textLabel.textAlignment = NSTextAlignmentCenter;
    }
    cell.textLabel.text = self.items[indexPath.row].name;
    return cell;
}

#pragma mark - UITableViewDelegate

- (void)tableView:(UITableView *)tableView didSelectRowAtIndexPath:(NSIndexPath *)indexPath {
    NSString *urlStr = self.items[indexPath.row].url;
    if (self.onItemClick) {
        self.onItemClick(urlStr);
    }
    [tableView deselectRowAtIndexPath:indexPath animated:YES];
}

@end
