//
//  iOSInternal.m
//  slark
//
//  Created by Nevermore on 2022/8/15.
//

#include "iOSInternal.h"
#include "Base.h"
#import <Foundation/NSObjCRuntime.h>
#import <Foundation/NSString.h>
#import <Foundation/NSPathUtilities.h>

void outputLog(std::string log){
    NSString* str = [NSString stringWithUTF8String:log.data()];
    NSLog(@"%@", str);
    str = nil;
}

namespace slark {

std::string rootPath() noexcept {
    static auto res = [] {
        NSString* homeDir = NSHomeDirectory();
        return std::string([homeDir UTF8String]);
        homeDir = nil;
    }();
    return res;
}

std::string tmpPath() noexcept {
    static auto res = [] {
        NSString* tmpDir = NSTemporaryDirectory();
        return std::string([tmpDir UTF8String]);
        tmpDir = nil;
    }();
    return res;
}

std::string cachePath() noexcept {
    static auto res = [] {
        NSArray* paths = NSSearchPathForDirectoriesInDomains(NSCachesDirectory, NSUserDomainMask, YES);
        NSString* cachesDir = [paths objectAtIndex:0];
        return std::string([cachesDir UTF8String]);
        cachesDir = nil;
        paths = nil;
    }();
    return res;
}

}
