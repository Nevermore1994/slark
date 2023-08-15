//
//  InternalLog.m
//  slark
//
//  Created by Nevermore on 2022/8/15.
//

#include "InternalLog.h"
#import <Foundation/NSObjCRuntime.h>
#import <Foundation/NSString.h>

void outputLog(std::string log){
    NSString* str = [NSString stringWithUTF8String:log.data()];
    NSLog(@"%@", str);
    str = nil;
}
