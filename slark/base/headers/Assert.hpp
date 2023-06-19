//
// Created by Nevermore on 2023/6/14.
// slark Assert
// Copyright (c) 2023 Nevermore All rights reserved.
//
#pragma once

#if DEBUG
void AssertMessage(bool result, const char* format, ...);

#define SAssert(result, format, args...) \
    do {                               \
        AssertMessage(result, "[assert] [%s][Line:%d][Function:%s]" format, __FILE__, __LINE__, __FUNCTION__, ##args);  \
    } while(0)
#else

#define SAssert(result, format, args...)   /*nothing*/

#endif