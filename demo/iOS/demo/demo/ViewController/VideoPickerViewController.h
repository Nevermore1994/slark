//
//  VideoPickerViewController.h
//  demo
//
//  Created by Nevermore- on 2025/7/25.
//

#ifndef VideoPickerViewController_h
#define VideoPickerViewController_h
#import <UIKit/UIKit.h>
#import <Photos/Photos.h>

typedef NS_ENUM(NSUInteger, PickerMode) {
    PickerModeSingle,
    PickerModeMultiple,
};

@interface VideoPickerViewController : UIViewController
@property (nonatomic, copy) void (^onResult)(NSArray<PHAsset*> *selectedVideos);
@property (nonatomic, assign, readonly) PickerMode pickerMode;

- (instancetype)initWithMode:(PickerMode) mode;
@end

#endif /* VideoPickerViewController_h */
