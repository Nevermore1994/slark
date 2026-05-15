//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#include "RawDecoder.h"

namespace slark {

bool RawDecoder::open(std::shared_ptr<DecoderConfig> config) noexcept {
    config_ = std::move(config);
    isOpen_ = true;
    isCompleted_ = false;
    return true;
}

void RawDecoder::close() noexcept {
    isOpen_ = false;
    isCompleted_ = false;
    config_.reset();
}

void RawDecoder::flush() noexcept {
    isCompleted_ = false;
}

void RawDecoder::reset() noexcept {
    isCompleted_ = false;
}

Error RawDecoder::decode(AVFrameRefPtr& frame) noexcept {
    if (!frame) {
        return Error::CodecInputError;
    }
    if (frame->info && frame->info->isEndOfStream) {
        isCompleted_ = true;
    } else {
        invokeReceiveFunc(DecoderData{std::move(frame)});
    }
    return Error::Success;
}

} // namespace slark
