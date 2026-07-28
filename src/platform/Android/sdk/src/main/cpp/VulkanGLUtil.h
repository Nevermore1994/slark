//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//
#pragma once

#include <GLES2/gl2.h>
#include <vulkan/vulkan.h>

#include <cstdint>

namespace slark {

GLuint compileGLShader(GLenum type, const char* src) noexcept;

GLuint createGLProgram(const char* vert, const char* frag) noexcept;

VkShaderModule createShaderModule(VkDevice device, const uint32_t* code, size_t size) noexcept;

}  // namespace slark
