//
//  iOSMediaUtil.cpp
//  slark
//
//  Created by Nevermore on 2024/10/21.
//

#include "iOSMediaUtil.h"

namespace slark {

CMSampleBufferRef createSampleBuffer(CMFormatDescriptionRef fmtDesc, void* buff, size_t size) {
    @autoreleasepool {
        CMBlockBufferRef blockBuffer = nullptr;
        CMSampleBufferRef sampleBuffer = nullptr;
        auto status = CMBlockBufferCreateWithMemoryBlock(NULL, buff, size, kCFAllocatorNull, NULL, 0, size, FALSE, &blockBuffer);
        
        if (status == noErr) {
            status = CMSampleBufferCreate(NULL, blockBuffer, TRUE, 0, 0, fmtDesc, 1, 0, NULL, 0, NULL, &sampleBuffer);
            
        }
        
        if (blockBuffer) {
            CFRelease(blockBuffer);
        }
        return status == noErr ? sampleBuffer : nullptr;
    }
}

UIImage* imageFromPixelBuffer(CVPixelBufferRef pixelBuffer) {
    if (pixelBuffer == NULL) {
        return NULL;
    }
    CVPixelBufferLockBaseAddress(pixelBuffer, kCVPixelBufferLock_ReadOnly);
    void *baseAddress = CVPixelBufferGetBaseAddress(pixelBuffer);
    
    size_t width = CVPixelBufferGetWidth(pixelBuffer);
    size_t height = CVPixelBufferGetHeight(pixelBuffer);
    size_t bytesPerRow = 4 * width;
    CGColorSpaceRef colorSpace = CGColorSpaceCreateDeviceRGB();
    
    CGBitmapInfo info = kCGImageAlphaPremultipliedFirst | kCGBitmapByteOrder32Little;
    CGContextRef context = CGBitmapContextCreate(baseAddress, width, height, 8, bytesPerRow, colorSpace, info);
    
    CGImageRef cgImage = CGBitmapContextCreateImage(context);
    UIImage *image = [UIImage imageWithCGImage:cgImage];
    
    CGImageRelease(cgImage);
    CGContextRelease(context);
    CGColorSpaceRelease(colorSpace);
    
    CVPixelBufferUnlockBaseAddress(pixelBuffer, kCVPixelBufferLock_ReadOnly);
    
    return image;
}

}

