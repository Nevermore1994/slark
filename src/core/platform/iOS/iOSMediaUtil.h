//
//  iOSMediaUtil.h
//  slark
//
//  Created by Nevermore on 2024/10/21.
//

#ifndef iOSMediaUtil_h
#define iOSMediaUtil_h

#import <VideoToolbox/VideoToolbox.h>

namespace slark {

CMSampleBufferRef createSampleBuffer(CMFormatDescriptionRef fmtDesc, void* buff, size_t size);

UIImage* imageFromPixelBuffer(CVPixelBufferRef pixelBuffer);
}

#endif /* iOSMediaUtil_h */
