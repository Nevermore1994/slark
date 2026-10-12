
//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#include "iOSMediaUtil.h"

namespace slark {

CMSampleBufferRef createSampleBuffer(CMFormatDescriptionRef fmtDesc, void* buff, size_t size) {
    @autoreleasepool {
        CMBlockBufferRef blockBuffer = nullptr;
        CMSampleBufferRef sampleBuffer = nullptr;
        auto status = CMBlockBufferCreateWithMemoryBlock(NULL, buff, size, kCFAllocatorNull, NULL,
                                                         0, size, FALSE, &blockBuffer);

        if (status == noErr) {
            status = CMSampleBufferCreate(NULL, blockBuffer, TRUE, 0, 0, fmtDesc, 1, 0, NULL, 0,
                                          NULL, &sampleBuffer);
        }

        if (blockBuffer) {
            CFRelease(blockBuffer);
        }
        return status == noErr ? sampleBuffer : nullptr;
    }
}

}  // namespace slark
