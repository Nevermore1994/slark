//
//  Util.cpp
//  slark
//
//  Created by Nevermore on 2023/10/20.
//  Copyright (c) 2023 Nevermore All rights reserved.

#include "Util.hpp"
#include "Random.hpp"

namespace slark::Util {

std::string genRandomName(const std::string& namePrefix) noexcept {
    return namePrefix + Random::randomString(8);
}

}

