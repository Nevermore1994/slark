//
//  SlarkRenderView.hpp
//  slark
//
//  Created by Nevermore on 2024/10/30.
//

#ifndef RenderGLView_hpp
#define RenderGLView_hpp
#import <UIKit/UIKit.h>
#import "IEGLContext.h"

namespace slark {

struct IVideoRender;

}
@protocol SlarkRenderViewDelegate <NSObject>
- (CVPixelBufferRef)requestRender;
@end

@interface SlarkRenderView : UIView
@property(nonatomic, assign) NSInteger renderInterval;
@property(nonatomic, assign) BOOL isActive;
@property(nonatomic, weak) id<SlarkRenderViewDelegate> delegate;

- (instancetype)initWithFrame:(CGRect) frame;
- (void)start;
- (void)stop;
- (CGFloat)scale;
- (void)updateRenderRect;
- (void)setContext:(slark::IEGLContextRefPtr) context;
- (std::weak_ptr<slark::IVideoRender>)renderImpl;
@end
#endif /* RenderGLView_hpp */
