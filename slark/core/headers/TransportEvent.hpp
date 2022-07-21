//
// Created by Nevermore on 2022/5/30.
// slark TransportEvent
// Copyright (c) 2022 Nevermore All rights reserved.
//
#pragma once

namespace slark{

enum class TransportEvent{
    Unknown,
    Start,
    Pause,
    Reset,
    Stop,
};

struct ITransportObserver{
    virtual void updateEvent(TransportEvent event) = 0;
};

}

