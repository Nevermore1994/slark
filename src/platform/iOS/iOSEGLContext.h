//
//  iOSEGLContext.hpp
//  slark
//
//  Created by Nevermore on 2024/10/30.
//

#ifndef iOSEGLContext_hpp
#define iOSEGLContext_hpp

#include "IEGLContext.h"

namespace slark {

class iOSEGLContext : public IEGLContext {
public:
    iOSEGLContext();
    
    ~iOSEGLContext() override;
    
    virtual bool init(void* context = nullptr) override;
    
    virtual void release() override;
    
    virtual void attachContext() override;
    
    virtual void detachContext() override;
    
    virtual void* nativeContext() override;
private:
    bool inited_ = false;
    void* context_ = nullptr;
};

}


#endif /* iOSEGLContext_hpp */
