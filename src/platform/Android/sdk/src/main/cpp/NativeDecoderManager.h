//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#pragma once

#include "IDecoder.h"
#include "Manager.hpp"

namespace slark {

class NativeDecoder : public IDecoder {
public:
    explicit NativeDecoder(DecoderType type) : IDecoder(type) {}

    ~NativeDecoder() override = default;

    void flush() noexcept override;

    void reset() noexcept override;

    void close() noexcept override;

    virtual void receiveDecodedData(DataPtr data, int64_t pts, bool isCompleted) noexcept = 0;

    virtual void onError(Error error) noexcept {
        invokeReceiveFunc(DecoderData{nullptr, error});
    }

    virtual void onVideoSizeChanged(uint32_t /*width*/, uint32_t /*height*/) noexcept {}

protected:
    std::string decoderId_;
};

using NativeDecoderManager = Manager<NativeDecoder>;

} // namespace slark