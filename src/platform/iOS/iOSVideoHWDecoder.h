//
//  iOSVideoHWDecoder.h
//  slark
//
//  Created by Nevermore on 2024/10/20.
//

#ifndef iOSVideoHWDecoder_h
#define iOSVideoHWDecoder_h

#import <VideoToolbox/VideoToolbox.h>
#include "IDecoder.h"
#include "MediaDefs.h"

namespace slark {

class iOSVideoHWDecoder : public IDecoder {
public:
    iOSVideoHWDecoder()
        :IDecoder(DecoderType::VideoHardWareDecoder) {
    }
    
    ~iOSVideoHWDecoder() override;
    
    void reset() noexcept override;
    
    DecoderErrorCode decode(AVFrameRefPtr& frame) noexcept override;

    void flush() noexcept override;
    
    bool open(std::shared_ptr<DecoderConfig> config) noexcept override;
    
    void close() noexcept override;
    
    inline static const DecoderTypeInfo& info() noexcept {
        static DecoderTypeInfo info = {
            DecoderType::RAW,
            RegisterClass(iOSVideoHWDecoder)
        };
        return info;
    }
private:
    bool createDecodeSession() noexcept;
private:
    VTDecompressionSessionRef decodeSession_ = nullptr;
    CMFormatDescriptionRef videoFormatDescription_ = nullptr;
};

}

#endif /* iOSVideoHWDecoder_h */
