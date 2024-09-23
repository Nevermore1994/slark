//
//  iOSBase.m
//  slark
//
//  Created by Nevermore on 2022/8/15.
//
#import <Foundation/NSObjCRuntime.h>
#import <Foundation/NSString.h>
#import <Foundation/NSPathUtilities.h>
#include "iOSBase.h"
#include "Base.h"
#include "FileUtil.h"

void outputLog(std::string log){
    NSString* str = [NSString stringWithUTF8String:log.data()];
    NSLog(@"%@", str);
    str = nil;
}

namespace slark::FileUtil {

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
        auto path = std::string([cachesDir UTF8String]);
        cachesDir = nil;
        paths = nil;
        return path;
    }();
    return res;
}

}
