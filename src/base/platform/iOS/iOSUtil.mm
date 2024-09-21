//
//  iOSUtil.m
//  slark
//
//  Created by Rolf.Tan on 
//
#import "iOSUtil.h"

bool isIphoneXSeries()
{
    bool isIphoneXSeries = false;
    if (@available(iOS 11.0, *)) {
        if ([[UIApplication sharedApplication] delegate].window.safeAreaInsets.bottom > 0.0) {
            isIphoneXSeries = YES;
        }
    }
    return isIphoneXSeries;
}

NSString* stringViewToNSString(std::string_view view) {
    return [[NSString alloc] initWithBytes:view.data()
                                    length:view.size()
                                  encoding:NSUTF8StringEncoding];
}
