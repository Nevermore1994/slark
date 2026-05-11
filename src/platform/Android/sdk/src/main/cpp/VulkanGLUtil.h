//
// Created by Nevermore- on 2025/5/6.
//
#pragma once

#include <GLES2/gl2.h>
#include <cstdint>
#include <vulkan/vulkan.h>

namespace slark {

GLuint compileGLShader(GLenum type, const char* src) noexcept;

GLuint createGLProgram(const char* vert, const char* frag) noexcept;

VkShaderModule createShaderModule(VkDevice device, const uint32_t* code, size_t size) noexcept;

} // namespace slark
