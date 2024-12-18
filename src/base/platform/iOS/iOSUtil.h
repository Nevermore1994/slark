//
//  iOSUtil.h
//  slark
//
//  Created by Nevermore
//
#ifndef slark_iOS_Util_h
#define slark_iOS_Util_h

#import <Foundation/Foundation.h>
#include <string_view>

#define Screen_Width CGRectGetWidth([UIScreen mainScreen].bounds)
#define Screen_Height CGRectGetHeight([UIScreen mainScreen].bounds)

#define iPhoneXNavigationHeight 88.0f
#define iPhoneXStatusBarHeight 44.0f
#define iPhoneXStatusHomeIndicatorHeight 34.0f
#define iPhoneNavigationHeight 64.0f
#define iPhoneStatusBarHeight 20.0f

#define iPhoneNavigationBarHeight 44.0f //Same for all iPhone models

#define NavigationHeight (isIphoneXSeries() ? iPhoneXNavigationHeight : iPhoneNavigationHeight)

#define StatusBarHeight (isIphoneXSeries() ? iPhoneXStatusBarHeight : iPhoneStatusBarHeight)

#define SafeBottomHeight (isIphoneXSeries() ? iPhoneXStatusHomeIndicatorHeight : 0)

bool isIphoneXSeries();
NSString* stringViewToNSString(std::string_view view);

#endif /* slark_iOS_Util_h */
