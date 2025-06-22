//
//  GLContextManager.h
//  slark
//
//  Created by Nevermore on 2024/11/7.
//

#include <string_view>
#include <unordered_map>
#include "NonCopyable.h"
#include "IEGLContext.h"
#include "Synchronized.hpp"

namespace slark {

class GLContextManager : public NonCopyable {
public:
    static GLContextManager& shareInstance() noexcept;
private:
    GLContextManager() = default;
public:
    ~GLContextManager() override = default;
    
    void addMainContext(const std::string& id, IEGLContextRefPtr context);

    void removeMainContext(const std::string& id);

    IEGLContextRefPtr getMainContext(const std::string& id) noexcept;

    IEGLContextRefPtr createShareContextWithId(const std::string& id)  noexcept;
private:
    Synchronized<std::unordered_map<std::string, IEGLContextRefPtr>, std::shared_mutex> contexts_;
};

}
