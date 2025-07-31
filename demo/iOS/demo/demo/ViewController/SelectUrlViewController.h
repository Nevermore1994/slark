//
//  SelectUrlViewController.h
//  demo
//
//  Created by Nevermore- on 2025/7/29.
//

#ifndef SelectUrlViewController_h
#define SelectUrlViewController_h

#import <UIKit/UIKit.h>

@interface SelectUrlViewController : UIViewController <UITableViewDelegate, UITableViewDataSource>

@property (nonatomic, copy) void (^onItemClick)(NSString* url);

@end

#endif /* SelectUrlViewController_h */
