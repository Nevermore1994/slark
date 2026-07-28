//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#pragma once
#include <string>
#include <unordered_map>

#include "IEGLContext.h"
#include "NonCopyable.h"
#include "Synchronized.hpp"

namespace slark {

class GLContextManager : public NonCopyMovable {
 public:
    static GLContextManager& shareInstance() noexcept;

 private:
    GLContextManager() = default;

 public:
    ~GLContextManager() = default;

    void addMainContext(const std::string& id, IEGLContextRefPtr context);

    void removeMainContext(const std::string& id);

    IEGLContextRefPtr getMainContext(const std::string& id) noexcept;

    IEGLContextRefPtr createShareContextWithId(const std::string& id) noexcept;

 private:
    Synchronized<std::unordered_map<std::string, IEGLContextRefPtr>, std::shared_mutex> contexts_;
};

}  // namespace slark
