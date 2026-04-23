//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#include "NativeDecoderManager.h"

namespace slark {

class AACHardwareDecoder : public NativeDecoder,
                           public std::enable_shared_from_this<AACHardwareDecoder> {
public:
    AACHardwareDecoder() : NativeDecoder(DecoderType::AACHardwareDecoder) {}

    ~AACHardwareDecoder() override = default;

    Error decode(AVFrameRefPtr& frame) noexcept override;

    bool open(std::shared_ptr<DecoderConfig> config) noexcept override;

    void flush() noexcept override;

    SLARK_DECODER_INFO(AACHardwareDecoder, DecoderType::AACHardwareDecoder)

    void receiveDecodedData(DataPtr data, int64_t pts, bool isCompleted) noexcept override;
};

} // namespace slark
