//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//
#include "GLShader.h"

namespace slark::GLShader {

const std::string_view vertexShader =
    "attribute vec4 position;"
    "attribute vec2 texCoord;"
    "uniform float preferredRotation;"
    "varying vec2 texCoordVarying;"
    "void main()"
    "{"
    "    mat4 rotationMatrix = mat4(cos(preferredRotation), -sin(preferredRotation), 0.0, 0.0,"
    "                               sin(preferredRotation),  cos(preferredRotation), 0.0, 0.0,"
    "                               0.0,                        0.0, 1.0, 0.0,"
    "                               0.0,                        0.0, 0.0, 1.0);"
    "    gl_Position = position * rotationMatrix;"
    "    texCoordVarying = texCoord;"
    "}";

const std::string_view fragmentShader =
    "varying highp vec2 texCoordVarying;"
    "precision mediump float;"
    "uniform sampler2D SamplerY;"
    "uniform sampler2D SamplerUV;"
    "uniform mat3 colorConversionMatrix;"
    "uniform float yOffset;"
    "void main()"
    "{"
    "    mediump vec3 yuv;"
    "    lowp vec3 rgb;"
    "    yuv.x = (texture2D(SamplerY, texCoordVarying).r - yOffset);"
    "    yuv.yz = (texture2D(SamplerUV, texCoordVarying).rg - vec2(0.5, 0.5));"
    "    rgb = colorConversionMatrix * yuv;"
    "    gl_FragColor = vec4(rgb, 1);"
    "}";

}  // namespace slark::GLShader