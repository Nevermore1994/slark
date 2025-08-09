//
//  RenderGLView.h
//  slark
//
//  Created by Nevermore on 2024/10/30.
//

#ifndef RenderGLView_h
#define RenderGLView_h
#import <UIKit/UIKit.h>
#import "IEGLContext.h"
#import "RenderGLViewDelegate.h"

namespace slark {

class IVideoRender;

}

@interface RenderGLView : UIView <RenderGLViewDelegate>
@property(atomic, assign, readonly) BOOL isActive;
@property(nonatomic, assign) double rotation;

- (instancetype)initWithFrame:(CGRect) frame;
- (void)start;
- (void)stop;
- (void)setContext:(slark::IEGLContextRefPtr) context;
- (std::weak_ptr<slark::IVideoRender>)renderImpl;
@end
#endif /* RenderGLView_h */
