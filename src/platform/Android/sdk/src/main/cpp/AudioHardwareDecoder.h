//
// Created by Nevermore on 2025/3/9.
//

#include "NativeDecoderManager.h"

namespace slark {

class AudioHardwareDecoder : public NativeDecoder,
                             public std::enable_shared_from_this<AudioHardwareDecoder> {
public:
    AudioHardwareDecoder()
        : NativeDecoder(DecoderType::AACHardwareDecoder) {

    }

    ~AudioHardwareDecoder() override = default;

    DecoderErrorCode decode(AVFrameRefPtr &frame) noexcept override;

    bool open(std::shared_ptr<DecoderConfig> config) noexcept override;

    void flush() noexcept override;

    inline static const DecoderTypeInfo &info() noexcept {
        static DecoderTypeInfo info = {
            DecoderType::RAW,
            BaseClass::registerClass<AudioHardwareDecoder>(GetClassName(AudioHardwareDecoder))
        };
        return info;
    }

    void receiveDecodedData(
        DataPtr data,
        int64_t pts,
        bool isCompleted
    ) noexcept override;
};

} // slark

