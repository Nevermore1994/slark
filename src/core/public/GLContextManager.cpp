//
//  GLContextManager.cpp
//  slark
//
//  Created by Nevermore on 2024/11/7.
//

#include <mutex>
#include "GLContextManager.h"

namespace slark {

GLContextManager& GLContextManager::shareInstance() noexcept {
    static auto manager_ = std::unique_ptr<GLContextManager>(new GLContextManager());
    return *manager_;
}

void GLContextManager::addMainContext(const std::string& id, IEGLContextRefPtr context) {
    contexts_.withWriteLock([&id, context = std::move(context)](auto& contexts){
        contexts[id] = context;
    });
}

void GLContextManager::removeMainContext(const std::string& id) {
    contexts_.withWriteLock([&id](auto& contexts){
        contexts.erase(id);
    });
}

IEGLContextRefPtr GLContextManager::getMainContext(const std::string& id) noexcept {
    IEGLContextRefPtr res;
    contexts_.withReadLock([&id, &res](auto& contexts){
        if (contexts.count(id) == 0) {
            return;
        }
        res = contexts.at(id);
    });
    return res;
}

IEGLContextRefPtr GLContextManager::createShareContextWithId(const std::string& id) noexcept {
    auto ptr = getMainContext(id);
    auto res = std::shared_ptr<IEGLContext>(createGLContext());
    res->init(ptr->nativeContext());
    return res;
}

#if !SLARK_IOS && !SLARK_ANDROID
IEGLContextPtr createGLContext() {
    return nullptr;
}
#endif
}
