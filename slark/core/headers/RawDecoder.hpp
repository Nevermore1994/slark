//
//  RawDecoder.hpp
//  Slark
//
//  Created by Nevermore on 2022/4/26.
//

#pragma once

#include "IDecoder.hpp"

namespace Slark {

class RawDecoder : public IDecoder {

public:
    ~RawDecoder() = default;

    void open() noexcept override;

    void close() noexcept override;

    void reset() noexcept override;

    AVFrameList decode(AVFrameList&& frameList) override;
};

}
