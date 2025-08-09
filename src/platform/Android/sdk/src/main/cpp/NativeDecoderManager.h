//
// Created by Nevermore on 2025/3/6.
//

#pragma once

#include "IDecoder.h"
#include "Manager.hpp"

namespace slark {

class NativeDecoder : public IDecoder {
public:
    explicit NativeDecoder(DecoderType type)
        : IDecoder(type) {

    }

    ~NativeDecoder() override = default;

    void flush() noexcept override;

    void reset() noexcept override;

    void close() noexcept override;

    virtual void receiveDecodedData(DataPtr data,
                                    int64_t pts,
                                    bool isCompleted) noexcept = 0;

protected:
    std::string decoderId_;
};

using NativeDecoderManager = Manager<NativeDecoder>;

}