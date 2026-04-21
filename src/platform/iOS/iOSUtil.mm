
//
// Copyright (c) 2022-2026 Nevermore1994. All rights reserved.
//
// This file is part of Slark.
// Licensed under the MIT License.
//

#import "iOSUtil.h"

#import <UIKit/UIKit.h>

BOOL isIphoneXSeries() {
    BOOL isIphoneXSeries = NO;
    if (@available(iOS 11.0, *)) {
        if ([[UIApplication sharedApplication] delegate].window.safeAreaInsets.bottom > 0.0) {
            isIphoneXSeries = YES;
        }
    }
    return isIphoneXSeries;
}
