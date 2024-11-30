//
//  GLProgram.h
//  slark
//
//  Created by Nevermore on 2024/11/1.
//

#pragma once

#if SLARK_IOS
#include <OpenGLES/ES2/gl.h>
#include <OpenGLES/ES2/glext.h>
#elif SLARK_ANDROID
#include <GLES/gl2.h>
#include <GLES/gl2ext.h>
#endif

#include <string>
#include <string_view>
#include <memory>

namespace slark {

enum class ShaderType {
    Vertex,
    Fragment,
};

class Shader {
public:
    Shader(ShaderType type, std::string_view source);
    ~Shader();
    
    ShaderType type() const noexcept {
        return type_;
    }
    
    GLuint shader() const noexcept {
        return shader_;
    }
    
    bool compile() noexcept;
private:
    ShaderType type_;
    std::string shaderString_;
    GLuint shader_;
};

class GLProgram {
public:
    GLProgram(std::string_view vertexShaderString, std::string_view fragmentShaderString);
    ~GLProgram();
    
    void attach() noexcept;
    
    void detach() noexcept;
public:
    bool isValid() const noexcept {
        return isValid_;
    }
    std::string_view id() const noexcept {
        return id_;
    }
    
    GLuint program() const noexcept {
        return program_;
    }
    
    int getLocation(const std::string& name) {
        if(program_ == 0) {
            return -1;
        }
        return glGetUniformLocation(program_, name.data());
    }
    
private:
    bool compile() noexcept;
    void deleteProgram() noexcept;
private:
    bool isValid_ = false;
    std::string id_;
    GLuint program_;
};

}

