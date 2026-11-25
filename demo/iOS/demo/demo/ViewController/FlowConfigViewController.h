//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#import <UIKit/UIKit.h>

@class SlarkFlowConfig;

@interface DemoFlowConfig : NSObject
@property (nonatomic, strong) SlarkFlowConfig* config;
@property (nonatomic, strong, readonly) SlarkFlowConfig* current;
+ (instancetype)shared;
@end

@interface FlowConfigViewController : UIViewController
@property (nonatomic, copy) void (^onApply)(void);
@property (nonatomic, copy) void (^onCancel)(void);
@end
