//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#ifndef SelectUrlViewController_h
#define SelectUrlViewController_h

#import <UIKit/UIKit.h>

@interface SelectUrlViewController : UIViewController <UITableViewDelegate, UITableViewDataSource>

@property (nonatomic, copy) void (^onItemClick)(NSString* url);

@end

#endif /* SelectUrlViewController_h */
