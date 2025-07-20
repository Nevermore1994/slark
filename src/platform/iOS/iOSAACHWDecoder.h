//
//  iOSAACHWDecoder.h
//  slark
//
//  Created by Nevermore on 2024/10/25.
//

#ifndef iOSAACHWDecoder_h
#define iOSAACHWDecoder_h

#include <AudioToolbox/AudioToolbox.h>
#include <condition_variable>
#include "MediaDefs.h"
#include "IDecoder.h"
#include "DecoderConfig.h"
#include "Synchronized.hpp"

namespace slark {

class iOSAACHWDecoder : public IDecoder {
public:
    iOSAACHWDecoder();
    
    ~iOSAACHWDecoder() override;
    
    void reset() noexcept override;
    
    DecoderErrorCode decode(AVFrameRefPtr& frame) noexcept override;

    void flush() noexcept override;
    
    bool open(std::shared_ptr<DecoderConfig> config) noexcept override;
    
    void close() noexcept override;
    
    inline static const DecoderTypeInfo& info() noexcept {
        static DecoderTypeInfo info = {
            DecoderType::AACHardwareDecoder,
            RegisterClass(iOSAACHWDecoder)
        };
        return info;
    }
    
    AVFrameRefPtr getDecodingFrame() noexcept;
    
private:
    AudioConverterRef decodeSession_ = nullptr;
    AVFrameRefPtr decodingFrame_ = nullptr;
    std::unique_ptr<AudioBufferList> outputData_;
};


}

#endif /* iOSAACHWDecoder_h */
