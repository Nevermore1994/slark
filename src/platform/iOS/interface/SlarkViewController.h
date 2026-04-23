//
//  SlarkViewController.h
//  slark
//
//  Created by Nevermore on 2024/11/6.
//

#ifndef SlarkViewController_h
#define SlarkViewController_h
#import "SlarkPlayer.h"

#import <UIKit/UIKit.h>

@interface SlarkViewController : UIViewController
@property (nonatomic, strong, readonly) SlarkPlayer* player;
- (instancetype)initWithPlayer:(CGRect)frame player:(SlarkPlayer*)player;
@end

#endif /* SlarkViewController_h */
