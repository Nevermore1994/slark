//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#ifndef VideoPickerViewController_h
#define VideoPickerViewController_h
#import <Photos/Photos.h>
#import <UIKit/UIKit.h>

typedef NS_ENUM(NSUInteger, PickerMode) {
    PickerModeSingle,
    PickerModeMultiple,
};

@interface VideoPickerViewController : UIViewController
@property (nonatomic, copy) void (^onResult)(NSArray<PHAsset*>* selectedVideos);
@property (nonatomic, assign, readonly) PickerMode pickerMode;

- (instancetype)initWithMode:(PickerMode)mode;
@end

#endif /* VideoPickerViewController_h */
