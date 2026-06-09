//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//
#ifndef SLARK_IOS_UTIL_H
#define SLARK_IOS_UTIL_H

#import <UIKit/UIKit.h>

#define SCREEN_WIDTH CGRectGetWidth([UIScreen mainScreen].bounds)
#define SCREEN_HEIGHT CGRectGetHeight([UIScreen mainScreen].bounds)

#define IS_IPHONE_X_SERIES      \
    (@available(iOS 11.0, *) && \
     [[UIApplication sharedApplication] delegate].window.safeAreaInsets.bottom > 0.0)

#define IPHONE_X_NAVIGATION_HEIGHT 88.0f
#define IPHONE_X_STATUS_BAR_HEIGHT 44.0f
#define IPHONE_X_HOME_INDICATOR_HEIGHT 34.0f
#define IPHONE_NAVIGATION_HEIGHT 64.0f
#define IPHONE_STATUS_BAR_HEIGHT 20.0f
#define IPHONE_NAVIGATION_BAR_HEIGHT 44.0f

#define NAVIGATION_HEIGHT \
    (IS_IPHONE_X_SERIES ? IPHONE_X_NAVIGATION_HEIGHT : IPHONE_NAVIGATION_HEIGHT)
#define STATUS_BAR_HEIGHT \
    (IS_IPHONE_X_SERIES ? IPHONE_X_STATUS_BAR_HEIGHT : IPHONE_STATUS_BAR_HEIGHT)
#define SAFE_BOTTOM_HEIGHT (IS_IPHONE_X_SERIES ? IPHONE_X_HOME_INDICATOR_HEIGHT : 0.0f)

#endif /* SLARK_IOS_UTIL_H */