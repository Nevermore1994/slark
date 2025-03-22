//
// Created by Nevermore on 2025/3/9.
//

#include "NativeDecoderManager.h"

namespace slark {

class AudioHardwareDecoder: public NativeDecoder,
        public std::enable_shared_from_this<AudioHardwareDecoder> {
public:
    bool decode(AVFramePtr frame) noexcept override;

    void flush() noexcept override;

    void reset() noexcept override;

    bool open(std::shared_ptr<DecoderConfig> config) noexcept override;

    void close() noexcept override;

    AVFramePtr getDecodingFrame(uint64_t pts) noexcept override;
private:
    std::string decoderId_;
    std::unordered_map<uint64_t, AVFramePtr> decodeFrames_;
};

} // slark

