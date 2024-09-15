//
//  main.cpp
//
//  Created by Nevermore on 2022/4/14.
//
#include "Player.h"
#include <cstdio>
#include <iostream>

using namespace slark;

int main() {
    auto item = std::make_unique<PlayerParams>();
    item->item.path = R"(/Users/rolf.tan/SourceCode/selfspace/slark/test_sample/sample-3s.wav)";
    Player player(std::move(item));
    player.play();
    getchar();
    return 0;
}
