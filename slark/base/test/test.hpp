//
// Created by Nevermore on 2021/10/22.
// slark test/test
// Copyright (c) 2021 Nevermore All rights reserved.
//

#pragma once

#include "testThread.hpp"
#include "testTimer.hpp"
#include "RingBuffer.hpp"
#include "File.hpp"
#include <cstdint>

using namespace slark;
using namespace slark::FileUtil;

inline void runTest() {
//    testThread();
//    testTimer();
    RingBuffer<uint8_t, 1024> buffer;
    File f("test.txt", std::ios_base::out | std::ios_base::binary);
    File ft("test1.txt", std::ios_base::out | std::ios_base::binary);
    f.open();
    ft.open();
    char d[13] ={'\0'};
    uint8_t* dp = reinterpret_cast<uint8_t*>(d);
    for(int i = 0; i < 1000; i++){
        auto str = Util::randomString(12);
        str.append("\n");
        auto p = reinterpret_cast<const uint8_t*>(str.c_str());
        buffer.append(p, str.length());
        f.stream().write(str.data(), str.length());
        auto t = buffer.read(dp, 13);
        ft.stream().write(reinterpret_cast<const char*>(dp), 13);
        printf("output %lld", t);
    }
    f.close();
    ft.close();
}
