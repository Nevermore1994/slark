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

    AVFramePtr getDecodingFrame(uint64_t pts) noexcept;

protected:
    std::string decoderId_;
    std::mutex mutex_;
    std::unordered_map<int64_t, AVFrameRefPtr> decodeFrames_;
};

using NativeDecoderManager = Manager<NativeDecoder>;

}