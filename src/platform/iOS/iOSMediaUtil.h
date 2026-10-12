//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//
#ifndef iOSMediaUtil_h
#define iOSMediaUtil_h

#import <VideoToolbox/VideoToolbox.h>

namespace slark {

CMSampleBufferRef createSampleBuffer(CMFormatDescriptionRef fmtDesc, void* buff, size_t size);
}  // namespace slark

#endif /* iOSMediaUtil_h */