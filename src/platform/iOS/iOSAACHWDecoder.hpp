//
//  iOSAACHWDecoder.hpp
//  slark
//
//  Created by Nevermore on 2024/10/25.
//

#ifndef iOSAACHWDecoder_hpp
#define iOSAACHWDecoder_hpp

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
    
    bool send(AVFramePtr frame) noexcept override ;

    void flush() noexcept override;
    
    bool open(std::shared_ptr<DecoderConfig> config) noexcept override;
    
    void close() noexcept override;
    
    inline static const DecoderTypeInfo& info() noexcept {
        static DecoderTypeInfo info = {
            DecoderType::AACHardwareDecoder,
            BaseClass<iOSAACHWDecoder>::registerClass(GetClassName(iOSAACHWDecoder))
        };
        return info;
    }
    
    AVFramePtr getDecodeFrame() noexcept;
    
    void setDecodeFrame(AVFramePtr ptr) noexcept {
        decodeFrame = std::move(ptr);
    }
private:
    AudioConverterRef decodeSession_ = nullptr;
    AVFramePtr decodeFrame = nullptr;
    std::unique_ptr<AudioBufferList> outputData_;
};


}

#endif /* iOSAACHWDecoder_hpp */
