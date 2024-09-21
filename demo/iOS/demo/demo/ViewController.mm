//
//  ViewController.m
//  demo
//
//  Created by Rolf.Tan on 2023/10/18.
//


#import "Masonry.h"
#import "ViewController.h"
#import "viewController/AudioViewController.h"
#import "iOSUtil.h"
#include "Log.hpp"

using namespace slark;
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
    LogI("viewDidLoad");
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
    self.tabDatas = [NSArray arrayWithObjects:@"test music", nil];
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
            AudioViewController* vc = [[AudioViewController alloc] init];
            [self.navigationController pushViewController:vc animated:YES];
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
