//
//  GLProgram.cpp
//  slark
//
//  Created by Nevermore on 2024/11/1.
//

#include "GLProgram.h"
#include "Log.hpp"
#include "Random.hpp"

namespace slark {

Shader::Shader(ShaderType type, std::string_view source)
    : type_(type)
    , shaderString_(source)
    , shader_(0){
}

Shader::~Shader() {
    if (shader_) {
        glDeleteShader(shader_);
        shader_ = 0;
    }
}

bool Shader::compile() noexcept {
    shader_ = glCreateShader(type_ == ShaderType::Vertex ? GL_VERTEX_SHADER : GL_FRAGMENT_SHADER);
    auto source = reinterpret_cast<const GLchar*>(shaderString_.c_str());
    glShaderSource(shader_, 1, &source, nullptr);
    glCompileShader(shader_);
    
    GLint status = GL_FALSE;
    glGetShaderiv(shader_, GL_COMPILE_STATUS, &status);
    if (status == GL_FALSE) {
        char error[1024] = {0};
        glGetShaderInfoLog(shader_, sizeof(error), NULL, error);
        glDeleteShader(shader_);
        shader_ = 0;
    }
    return status == GL_TRUE;
}

GLProgram::GLProgram(std::string_view vertexShaderString, std::string_view fragmentShaderString)
    : id_(Random::randomString(8))
    , program_(0) {
    auto vertexShader = std::make_unique<Shader>(ShaderType::Vertex, vertexShaderString);
    auto fragmentShader = std::make_unique<Shader>(ShaderType::Fragment, fragmentShaderString);
    if (!vertexShader || !fragmentShader) {
        return;
    }
    program_ = glCreateProgram();
    if (program_ == 0) {
        return;
    }
    if (!fragmentShader->compile() || !vertexShader->compile()) {
        return;
    }
    glAttachShader(program_, vertexShader->shader());
    glAttachShader(program_, fragmentShader->shader());
    
    glLinkProgram(program_);
    
    GLint status = GL_FALSE;
    glGetProgramiv(program_, GL_LINK_STATUS, &status);
    if (status == GL_FALSE) {
        char error[1024] = {0};
        glGetProgramInfoLog(program_, sizeof(error), NULL, error);
        LogI("link program error:{}", error);
    } else {
        glDetachShader(program_, vertexShader->shader());
        glDetachShader(program_, fragmentShader->shader());
        isValid_ = true;
    }
}

GLProgram::~GLProgram() {
    deleteProgram();
}

void GLProgram::attach() noexcept {
    glUseProgram(program_);
}

void GLProgram::detach() noexcept {
    glUseProgram(0);
}

void GLProgram::deleteProgram() noexcept {
    if (program_ == 0) {
        return;
    }
    glDeleteProgram(program_);
    program_ = 0;
}

}
