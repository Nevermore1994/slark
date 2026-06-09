//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//
#include "VulkanGLUtil.h"

#include "Log.hpp"

#include <GLES2/gl2.h>
#include <vulkan/vulkan.h>

namespace slark {

GLuint compileGLShader(GLenum type, const char* src) noexcept {
    GLuint shader = glCreateShader(type);
    if (!shader) {
        return 0;
    }
    glShaderSource(shader, 1, &src, nullptr);
    glCompileShader(shader);
    GLint ok = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char buf[512];
        glGetShaderInfoLog(shader, sizeof(buf), nullptr, buf);
        LogE("GL shader compile error: {}", buf);
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

GLuint createGLProgram(const char* vert, const char* frag) noexcept {
    GLuint vs = compileGLShader(GL_VERTEX_SHADER, vert);
    if (!vs) {
        return 0;
    }
    GLuint fs = compileGLShader(GL_FRAGMENT_SHADER, frag);
    if (!fs) {
        glDeleteShader(vs);
        return 0;
    }
    GLuint prog = glCreateProgram();
    glAttachShader(prog, vs);
    glAttachShader(prog, fs);
    glLinkProgram(prog);
    glDeleteShader(vs);
    glDeleteShader(fs);
    GLint ok = 0;
    glGetProgramiv(prog, GL_LINK_STATUS, &ok);
    if (!ok) {
        char buf[512];
        glGetProgramInfoLog(prog, sizeof(buf), nullptr, buf);
        LogE("GL program link error: {}", buf);
        glDeleteProgram(prog);
        return 0;
    }
    return prog;
}

VkShaderModule createShaderModule(VkDevice device, const uint32_t* code, size_t size) noexcept {
    VkShaderModuleCreateInfo ci{};
    ci.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    ci.codeSize = size;
    ci.pCode = code;
    VkShaderModule mod = VK_NULL_HANDLE;
    vkCreateShaderModule(device, &ci, nullptr, &mod);
    return mod;
}

} // namespace slark
