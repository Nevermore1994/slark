
//
// Copyright (c) 2022-2026 Nevermore1994. All rights reserved.
//
// This file is part of Slark.
// Licensed under the MIT License.
//

#import "AppDelegate.h"

#import <UIKit/UIKit.h>

int main(int argc, char* argv[]) {
    NSString* appDelegateClassName;
    @autoreleasepool {
        Setup code that might create autoreleased objects goes here.appDelegateClassName =
            NSStringFromClass([AppDelegate class]);
    }
    return UIApplicationMain(argc, argv, nil, appDelegateClassName);
}
