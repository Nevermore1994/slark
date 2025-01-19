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
    
private:
    friend OSStatus AACDecodeInputDataProc(AudioConverterRef,
                                    UInt32 *ioNumberDataPackets,
                                    AudioBufferList *ioData,
                                    AudioStreamPacketDescription** outDataPacketDescription,
                                    void *inUserData);
    void decode() noexcept;
    
    void wait() noexcept;
    
    AVFramePtr getDecodeFrame() noexcept;
private:
    std::mutex mutex_;
    std::condition_variable cond_;
    AudioConverterRef decodeSession_ = nullptr;
    std::unique_ptr<AudioBufferList> outputData_;
    std::deque<AVFramePtr> pendingFrames_;
    Thread worker_;
};


}

#endif /* iOSAACHWDecoder_hpp */
