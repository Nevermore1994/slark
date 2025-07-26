//
//  VideoPickerViewController.m
//  demo
//
//  Created by Nevermore- on 2025/7/25.
//

#import "VideoPickerViewController.h"
#import <Photos/Photos.h>

@interface VideoCell : UICollectionViewCell
@property (nonatomic, strong) UIImageView *thumbnailView;
@property (nonatomic, strong) UILabel *durationLabel;
@property (nonatomic, strong) UIView *selectedOverlay;
@end

@implementation VideoCell
- (instancetype)initWithFrame:(CGRect)frame {
    if (self = [super initWithFrame:frame]) {
        _thumbnailView = [[UIImageView alloc] initWithFrame:self.contentView.bounds];
        _thumbnailView.contentMode = UIViewContentModeScaleAspectFill;
        _thumbnailView.clipsToBounds = YES;
        [self.contentView addSubview:_thumbnailView];

        _durationLabel = [[UILabel alloc] initWithFrame:CGRectMake(5, self.contentView.bounds.size.height-22, self.contentView.bounds.size.width-10, 18)];
        _durationLabel.font = [UIFont systemFontOfSize:12];
        _durationLabel.textColor = [UIColor whiteColor];
        _durationLabel.backgroundColor = [[UIColor blackColor] colorWithAlphaComponent:0.5];
        _durationLabel.textAlignment = NSTextAlignmentRight;
        [self.contentView addSubview:_durationLabel];

        _selectedOverlay = [[UIView alloc] initWithFrame:self.contentView.bounds];
        _selectedOverlay.backgroundColor = [[UIColor whiteColor] colorWithAlphaComponent:0.55];
        _selectedOverlay.hidden = YES;
        [self.contentView addSubview:_selectedOverlay];
    }
    return self;
}
@end

@interface VideoPickerViewController () <UICollectionViewDelegate, UICollectionViewDataSource>
@property (nonatomic, strong) UICollectionView *collectionView;
@property (nonatomic, strong) NSMutableArray<PHAsset *> *videoAssets;
@property (nonatomic, strong) NSMutableArray<PHAsset *> *selectedAssets;
@property (nonatomic, assign) PickerMode pickerMode;
@property (nonatomic, strong) UIButton* doneBtn;
@end

@implementation VideoPickerViewController

- (instancetype)initWithMode:(PickerMode)mode {
    if (self = [super init]) {
        self.pickerMode = mode;
    }
    return self;
}

- (void)viewDidLoad {
    [super viewDidLoad];
    self.title = @"Select Video";
    self.view.backgroundColor = [UIColor whiteColor];
    self.videoAssets = [NSMutableArray array];
    self.selectedAssets = [NSMutableArray array];

    UICollectionViewFlowLayout *layout = [[UICollectionViewFlowLayout alloc] init];
    CGFloat itemW = ([UIScreen mainScreen].bounds.size.width - 15 - 10)/3.0;
    layout.itemSize = CGSizeMake(itemW, itemW);
    layout.minimumLineSpacing = 5;
    layout.minimumInteritemSpacing = 5;
    layout.sectionInset = UIEdgeInsetsMake(5, 5, 60, 5);

    self.collectionView = [[UICollectionView alloc] initWithFrame:self.view.bounds collectionViewLayout:layout];
    self.collectionView.backgroundColor = [UIColor whiteColor];
    self.collectionView.delegate = self;
    self.collectionView.dataSource = self;
    [self.collectionView registerClass:[VideoCell class] forCellWithReuseIdentifier:@"VideoCell"];
    [self.view addSubview:self.collectionView];

    if (self.pickerMode == PickerModeMultiple) {
        [self.view addSubview:self.doneBtn];
    }

    [self requestPermissionAndLoadVideos];
}

- (UIButton*)doneBtn {
    if (_doneBtn == nil) {
        UIButton* doneBtn = [UIButton buttonWithType:UIButtonTypeSystem];
        doneBtn.frame = CGRectMake(0, self.view.bounds.size.height-50, self.view.bounds.size.width, 50);
        doneBtn.backgroundColor = [UIColor systemBlueColor];
        [doneBtn setTitle:@"Confirm" forState:UIControlStateNormal];
        [doneBtn setTitleColor:[UIColor whiteColor] forState:UIControlStateNormal];
        [doneBtn addTarget:self action:@selector(doneAction) forControlEvents:UIControlEventTouchUpInside];
        _doneBtn = doneBtn;
    }
    return _doneBtn;
}

- (void)requestPermissionAndLoadVideos {
    PHAuthorizationStatus status = [PHPhotoLibrary authorizationStatus];
    if (status == PHAuthorizationStatusAuthorized) {
        [self loadVideos];
    } else {
        [PHPhotoLibrary requestAuthorization:^(PHAuthorizationStatus status) {
            if (status == PHAuthorizationStatusAuthorized) {
                dispatch_async(dispatch_get_main_queue(), ^{
                    [self loadVideos];
                });
            }
        }];
    }
}

- (void)loadVideos {
    PHFetchOptions *options = [[PHFetchOptions alloc] init];
    options.sortDescriptors = @[[NSSortDescriptor sortDescriptorWithKey:@"creationDate" ascending:NO]];
    options.predicate = [NSPredicate predicateWithFormat:@"mediaType == %d", PHAssetMediaTypeVideo];
    PHFetchResult *results = [PHAsset fetchAssetsWithOptions:options];
    [self.videoAssets removeAllObjects];
    for (PHAsset *asset in results) {
        [self.videoAssets addObject:asset];
    }
    [self.collectionView reloadData];
}

- (NSInteger)collectionView:(UICollectionView *)collectionView numberOfItemsInSection:(NSInteger)section {
    return self.videoAssets.count;
}

- (__kindof UICollectionViewCell *)collectionView:(UICollectionView *)collectionView cellForItemAtIndexPath:(NSIndexPath *)indexPath {
    VideoCell *cell = [collectionView dequeueReusableCellWithReuseIdentifier:@"VideoCell" forIndexPath:indexPath];
    PHAsset *asset = self.videoAssets[indexPath.item];
    int scale = [UIScreen mainScreen].scale;
    CGSize size = CGSizeMake(cell.bounds.size.width*scale, cell.bounds.size.height*scale);
    [[PHImageManager defaultManager] requestImageForAsset:asset targetSize:size contentMode:PHImageContentModeAspectFit options:nil resultHandler:^(UIImage *result, NSDictionary *info) {
        cell.thumbnailView.image = result;
    }];
    cell.durationLabel.text = [self formatDuration:asset.duration];
    cell.selectedOverlay.hidden = ![self.selectedAssets containsObject:asset];
    return cell;
}

- (void)collectionView:(UICollectionView *)collectionView didSelectItemAtIndexPath:(NSIndexPath *)indexPath {
    PHAsset *asset = self.videoAssets[indexPath.item];
    if (self.pickerMode == PickerModeSingle) {
        [self.selectedAssets addObject:asset];
        [self doneAction];
    } else {
        if ([self.selectedAssets containsObject:asset]) {
            [self.selectedAssets removeObject:asset];
        } else {
            [self.selectedAssets addObject:asset];
        }
        self.doneBtn.hidden = self.selectedAssets.count == 0;
        [collectionView reloadItemsAtIndexPaths:@[indexPath]];
    }
}

- (NSString *)formatDuration:(NSTimeInterval)duration {
    int min = (int)(duration/60);
    int sec = (int)duration%60;
    return [NSString stringWithFormat:@"%02d:%02d", min, sec];
}

- (void)doneAction {
    if (self.onResult && self.selectedAssets.count > 0) {
        NSMutableArray *urls = [NSMutableArray array];
        PHVideoRequestOptions *options = [[PHVideoRequestOptions alloc] init];
        options.version = PHVideoRequestOptionsVersionCurrent;
        dispatch_group_t group = dispatch_group_create();
        self.onResult(self.selectedAssets);
    }
}

@end
