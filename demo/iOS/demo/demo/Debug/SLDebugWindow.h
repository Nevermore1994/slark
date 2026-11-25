//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#import <UIKit/UIKit.h>

@interface SLDebugWindow : NSObject

@property (nonatomic, copy) NSString* (^snapshotProvider)(void);

+ (instancetype)shared;

- (void)start;

@end
