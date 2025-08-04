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

namespace slark {

class IVideoRender;

}

@protocol RenderViewDelegate <NSObject>
- (CVPixelBufferRef)requestRender;
@end

@interface RenderGLView : UIView
@property(atomic, assign, readonly) BOOL isActive;
@property(nonatomic, weak) id<RenderViewDelegate> delegate;

- (instancetype)initWithFrame:(CGRect) frame;
- (void)start;
- (void)stop;
- (CGFloat)scale;
- (void)setContext:(slark::IEGLContextRefPtr) context;
- (std::weak_ptr<slark::IVideoRender>)renderImpl;
@end
#endif /* RenderGLView_h */
