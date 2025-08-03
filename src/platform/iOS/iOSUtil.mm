//
//  iOSUtil.m
//  slark
//
//  Created by Nevermore
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
