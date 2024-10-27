//
//  iOSAACHWDecoder.hpp
//  slark
//
//  Created by Nevermore on 2024/10/25.
//

#ifndef iOSAACHWDecoder_hpp
#define iOSAACHWDecoder_hpp

#include <AudioToolbox/AudioToolbox.h>
#include "MediaDefs.h"
#include "IDecoder.h"
#include "RingBuffer.hpp"
#include "DecoderConfig.h"


namespace slark {

class iOSAACHWDecoder : public IDecoder {
public:
    iOSAACHWDecoder() {
        decoderType_ = DecoderType::AACHardwareDecoder;
    }
    
    ~iOSAACHWDecoder() override;
    
    void reset() noexcept override;
    
    bool send(AVFramePtr frame) noexcept override ;

    void flush() noexcept override;
    
    bool open(std::shared_ptr<DecoderConfig> config) noexcept override;
    
    void close() noexcept override;
    
    inline static const DecoderTypeInfo& info() noexcept {
        static DecoderTypeInfo info = {
            DecoderType::AACHardwareDecoder,
            MEDIA_MIMETYPE_AUDIO_AAC,
            BaseClass<iOSAACHWDecoder>::registerClass(GetClassName(iOSAACHWDecoder))
        };
        return info;
    }
    
private:
    AudioConverterRef decodeSession_ = nullptr;
    std::unique_ptr<AudioBufferList> outputData_;
};


}

#endif /* iOSAACHWDecoder_hpp */
