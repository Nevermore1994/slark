//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#pragma once

#include "AVFrame.hpp"
#include "Data.h"
#include "DecoderConfig.h"
#include "IDecoder.h"

#include <AudioToolbox/AudioToolbox.h>

namespace slark {

class AACHardwareDecoder : public IDecoder {
public:
    AACHardwareDecoder();
    ~AACHardwareDecoder() override;

    bool open(std::shared_ptr<DecoderConfig> config) noexcept override;
    void close() noexcept override;
    void reset() noexcept override;
    void flush() noexcept override;
    Error decode(AVFrameRefPtr& frame) noexcept override;

    inline static const DecoderTypeInfo& info() noexcept {
        static DecoderTypeInfo info = {
            DecoderType::AACHardwareDecoder,
            Reflection::enroll<AACHardwareDecoder>("AACHardwareDecoder")};
        return info;
    }

private:
    friend OSStatus AACDecodeInputDataProc(AudioConverterRef,
                                           UInt32*,
                                           AudioBufferList*,
                                           AudioStreamPacketDescription**,
                                           void*);

public:
    AVFrameRefPtr getDecodingFrame() noexcept {
        return decodingFrame_;
    }

    void retainInputData(std::unique_ptr<Data> data) noexcept {
        inputData_ = std::move(data);
    }

private:
    AudioConverterRef decodeSession_ = nullptr;
    AVFrameRefPtr decodingFrame_ = nullptr;
    std::unique_ptr<Data> inputData_;
    std::unique_ptr<AudioBufferList> outputData_;
    DataPtr outputBuffer_; // 预分配的输出 PCM buffer，跨帧复用
    DataPtr spareBuffer_;  // 双缓冲备用
};

} // namespace slark
