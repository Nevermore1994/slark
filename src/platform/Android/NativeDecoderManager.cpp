//
// Created by Nevermore on 2025/3/6.
//

#include "NativeDecoderManager.h"
#include <utility>
#include "NativeHardwareDecoder.h"

namespace slark {

void NativeDecoder::flush() noexcept {
    std::lock_guard lock(mutex_);
    decodeFrames_.clear();
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
    {
        std::lock_guard lock(mutex_);
        decodeFrames_.clear();
    }
}

AVFramePtr NativeDecoder::getDecodingFrame(uint64_t pts) noexcept {
    std::lock_guard lock(mutex_);
    if (decodeFrames_.contains(pts)) {
        auto ptr = std::move(decodeFrames_[pts]);
        decodeFrames_.erase(pts);
    }
    return nullptr;
}

NativeDecoderManager& NativeDecoderManager::shareInstance() noexcept {
    static auto instance_ = std::unique_ptr<NativeDecoderManager>(new NativeDecoderManager());
    return *instance_;
}

NativeDecoderRefPtr NativeDecoderManager::find(const std::string& decoderId) noexcept {
    std::lock_guard lock(mutex_);
    if (decoders_.contains(decoderId)) {
        return decoders_.at(decoderId);
    }
    return nullptr;
}

void NativeDecoderManager::add(const std::string& decoderId, NativeDecoderRefPtr decoder) noexcept {
    std::lock_guard lock(mutex_);
    decoders_[decoderId] = std::move(decoder);
}

void NativeDecoderManager::remove(const std::string& decoderId) noexcept {
    std::lock_guard lock(mutex_);
    decoders_.erase(decoderId);
}


}
