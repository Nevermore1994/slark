//
// Created by Nevermore on 2025/3/6.
//

#include "NativeDecoderManager.h"
#include <utility>
#include "NativeHardwareDecoder.h"

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
    LogI("release video decoder:{}",
         decoderId_);
    decoderId_.clear();
}

}
