//
//  IEGLContext.h
//  slark
//
//  Created by Nevermore on 2024/10/30.
//

#pragma once
#include <cstdint>
#include <memory>

namespace slark {

class IEGLContext {
public:
    virtual ~IEGLContext() = default;
    
    virtual bool init(void* context = nullptr) = 0;
    
    virtual void release() = 0;
    
    virtual bool createBuffer(uint32_t width, uint32_t height) = 0;
    
    virtual void acttachContext() = 0;
    
    virtual void detachContext() = 0;
    
    virtual void* nativeContext() = 0;
};

using IEGLContextPtr = std::unique_ptr<IEGLContext>;
using IEGLContextRefPtr = std::shared_ptr<IEGLContext>;

IEGLContextPtr createEGLContext();

}
