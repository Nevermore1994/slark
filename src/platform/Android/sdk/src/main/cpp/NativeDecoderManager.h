//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#pragma once

#include "IDecoder.h"
#include "Manager.hpp"
#include "NativeHardwareDecoder.h"

namespace slark {

class NativeDecoder : public IDecoder {
 public:
    explicit NativeDecoder(DecoderType type) : IDecoder(type) {}

    ~NativeDecoder() override = default;

    void flush() noexcept override;

    void reset() noexcept override;

    void close() noexcept override;

    virtual void receiveDecodedData(DataPtr data, int64_t pts, bool isCompleted) noexcept = 0;

    virtual void onError(Error error, int32_t origin = 0) noexcept {
        invokeReceiveFunc(DecoderData{nullptr, error, 0, origin});
    }

    virtual void onVideoSizeChanged(uint32_t /*width*/, uint32_t /*height*/) noexcept {}

    [[nodiscard]] std::string name() const noexcept override {
        auto codecName = NativeHardwareDecoder::decoderName(decoderId_);
        return codecName.empty() ? std::string(decoderTypeName(type())) : codecName;
    }

 protected:
    std::string decoderId_;
};

using NativeDecoderManager = Manager<NativeDecoder>;

}  // namespace slark