//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#include "NativeDecoderManager.h"

#include "NativeHardwareDecoder.h"

#include <utility>

namespace slark {

void NativeDecoder::flush() noexcept {
    NativeHardwareDecoder::flush(decoderId_);
}

void NativeDecoder::reset() noexcept {
    flush();
}

void NativeDecoder::close() noexcept {
    if (decoderId_.empty()) {
        return;
    }
    NativeHardwareDecoder::release(decoderId_);
    NativeDecoderManager::shareInstance().remove(decoderId_);
    LogI("release video decoder:{}", decoderId_);
    decoderId_.clear();
}

} // namespace slark
