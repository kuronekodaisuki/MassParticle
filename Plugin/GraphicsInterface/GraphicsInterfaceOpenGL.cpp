﻿#include "pch.h"
#include "giInternal.h"

#ifdef giSupportOpenGL
#ifdef _WIN32
    #pragma comment(lib, "opengl32.lib")
    #pragma comment(lib, "glew32s.lib")
    #define GLEW_STATIC
#endif
#include <GL/glew.h>

namespace gi {

class GraphicsInterfaceOpenGL : public GraphicsInterface
{
public:
    GraphicsInterfaceOpenGL(void *device);
    ~GraphicsInterfaceOpenGL() override;
    void release() override;

    void* getDevicePtr() override;
    DeviceType getDeviceType() override;
    void sync() override;

    Result createTexture2D(void **dst_tex, int width, int height, TextureFormat format, const void *data, ResourceFlags flags) override;
    void releaseTexture2D(void *tex) override;
    Result readTexture2D(void *o_buf, size_t bufsize, void *tex, int width, int height, TextureFormat format) override;
    Result writeTexture2D(void *o_tex, int width, int height, TextureFormat format, const void *buf, size_t bufsize) override;

    Result createBuffer(void **dst_buf, size_t size, BufferType type, const void *data, ResourceFlags flags) override;
    void releaseBuffer(void *buf) override;
    Result readBuffer(void *dst, void *src_buf, size_t read_size, BufferType type) override;
    Result writeBuffer(void *dst_buf, const void *src, size_t write_size, BufferType type) override;
};


GraphicsInterface* CreateGraphicsInterfaceOpenGL(void *device)
{
    return new GraphicsInterfaceOpenGL(device);
}


void* GraphicsInterfaceOpenGL::getDevicePtr() { return nullptr; }
DeviceType GraphicsInterfaceOpenGL::getDeviceType() { return DeviceType::OpenGL; }

GraphicsInterfaceOpenGL::GraphicsInterfaceOpenGL(void *device)
{
    glewInit();
}

GraphicsInterfaceOpenGL::~GraphicsInterfaceOpenGL()
{
}

void GraphicsInterfaceOpenGL::release()
{
    delete this;
}


static void GetGLTextureType(TextureFormat format, GLenum &glfmt, GLenum &gltype, GLenum &glitype)
{
    switch (format)
    {
    case TextureFormat::RGBAu8:   glfmt = GL_RGBA;  gltype = GL_UNSIGNED_BYTE; glitype = GL_R8; return;

    case TextureFormat::RGBAf16:  glfmt = GL_RGBA;  gltype = GL_HALF_FLOAT; glitype = GL_RGBA16F; return;
    case TextureFormat::RGf16:    glfmt = GL_RG;    gltype = GL_HALF_FLOAT; glitype = GL_RG16F; return;
    case TextureFormat::Rf16:     glfmt = GL_RED;   gltype = GL_HALF_FLOAT; glitype = GL_R16F; return;

    case TextureFormat::RGBAf32:  glfmt = GL_RGBA;  gltype = GL_FLOAT; glitype = GL_RGBA32F; return;
    case TextureFormat::RGf32:    glfmt = GL_RG;    gltype = GL_FLOAT; glitype = GL_RG32F; return;
    case TextureFormat::Rf32:     glfmt = GL_RED;   gltype = GL_FLOAT; glitype = GL_R32F; return;

    case TextureFormat::RGBAi32:  glfmt = GL_RGBA_INTEGER;  gltype = GL_INT;  glitype = GL_RGBA32I; return;
    case TextureFormat::RGi32:    glfmt = GL_RG_INTEGER;    gltype = GL_INT;  glitype = GL_RG32I; return;
    case TextureFormat::Ri32:     glfmt = GL_RED_INTEGER;   gltype = GL_INT;  glitype = GL_R32I; return;
    default: break;
    }
}

static Result GetGLError()
{
    auto e = glGetError();
    switch (e) {
    case GL_NO_ERROR: return Result::OK;
    case GL_OUT_OF_MEMORY: return Result::OutOfMemory;
    case GL_INVALID_ENUM: return Result::InvalidParameter;
    case GL_INVALID_VALUE: return Result::InvalidParameter;
    case GL_INVALID_OPERATION: return Result::InvalidOperation;
    }
    return Result::Unknown;
}

void GraphicsInterfaceOpenGL::sync()
{
    glFinish();
}

Result GraphicsInterfaceOpenGL::createTexture2D(void **dst_tex, int width, int height, TextureFormat format, const void *data, ResourceFlags flags)
{
    GLenum gl_format = 0;
    GLenum gl_type = 0;
    GLenum gl_itype = 0;
    GetGLTextureType(format, gl_format, gl_type, gl_itype);

    auto ret = Result::OK;
    GLuint tex = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexImage2D(GL_TEXTURE_2D, 0, gl_itype, width, height, 0, gl_format, gl_type, data);
    ret = GetGLError();
    glBindTexture(GL_TEXTURE_2D, 0);
    *(GLuint*)dst_tex = tex;

    return ret;
}

void GraphicsInterfaceOpenGL::releaseTexture2D(void *tex_)
{
    GLuint tex = (GLuint)(size_t)tex_;
    glDeleteTextures(1, &tex);

}

Result GraphicsInterfaceOpenGL::readTexture2D(void *o_buf, size_t, void *tex, int, int, TextureFormat format)
{
    GLenum gl_format = 0;
    GLenum gl_type = 0;
    GLenum gl_itype = 0;
    GetGLTextureType(format, gl_format, gl_type, gl_itype);

    // available OpenGL 4.5 or later
    // glGetTextureImage((GLuint)(size_t)tex, 0, internal_format, internal_type, bufsize, o_buf);

    sync();

    auto ret = Result::OK;
    glBindTexture(GL_TEXTURE_2D, (GLuint)(size_t)tex);
    glGetTexImage(GL_TEXTURE_2D, 0, gl_format, gl_type, o_buf);
    ret = GetGLError();
    glBindTexture(GL_TEXTURE_2D, 0);
    return ret;
}

Result GraphicsInterfaceOpenGL::writeTexture2D(void *o_tex, int width, int height, TextureFormat format, const void *buf, size_t)
{
    GLenum gl_format = 0;
    GLenum gl_type = 0;
    GLenum gl_itype = 0;
    GetGLTextureType(format, gl_format, gl_type, gl_itype);

    // available OpenGL 4.5 or later
    // glTextureSubImage2D((GLuint)(size_t)o_tex, 0, 0, 0, width, height, internal_format, internal_type, buf);

    auto ret = Result::OK;
    glBindTexture(GL_TEXTURE_2D, (GLuint)(size_t)o_tex);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width, height, gl_format, gl_type, buf);
    ret = GetGLError();
    glBindTexture(GL_TEXTURE_2D, 0);
    return ret;
}


static GLenum GetGLBufferType(BufferType type)
{
    GLenum gltype = 0;
    switch (type) {
    case BufferType::Index:
        gltype = GL_ELEMENT_ARRAY_BUFFER;
        break;
    case BufferType::Vertex:
        gltype = GL_ARRAY_BUFFER;
        break;
    case BufferType::Constant:
        gltype = GL_UNIFORM_BUFFER;
        break;
    case BufferType::Compute:
        gltype = GL_SHADER_STORAGE_BUFFER;
        break;
    }
    return gltype;
}

Result GraphicsInterfaceOpenGL::createBuffer(void **dst_buf, size_t size, BufferType type, const void *data, ResourceFlags flags)
{
    GLenum gltype = GetGLBufferType(type);
    GLenum glusage = GL_STATIC_DRAW;
    if (flags & ResourceFlags::CPU_Write) {
        glusage = GL_DYNAMIC_DRAW;
    }
    if (flags & ResourceFlags::CPU_Read) {
        glusage = GL_STREAM_DRAW;
    }

    auto ret = Result::OK;
    GLuint buf = 0;
    glGenBuffers(1, &buf);
    glBindBuffer(gltype, buf);
    glBufferData(gltype, size, data, glusage);
    ret = GetGLError();
    glBindBuffer(gltype, 0);

    *(GLuint*)dst_buf = buf;
    return ret;
}

void GraphicsInterfaceOpenGL::releaseBuffer(void *buf_)
{
    GLuint buf = (GLuint)(size_t)buf_;
    glDeleteBuffers(1, &buf);
}

Result GraphicsInterfaceOpenGL::readBuffer(void *dst, void *src_buf, size_t read_size, BufferType type)
{
    GLuint buf = (GLuint)(size_t)src_buf;
    GLenum gltype = GetGLBufferType(type);

    Result ret = Result::OK;
    glBindBuffer(gltype, buf);
    void *mapped_data = glMapBuffer(gltype, GL_READ_ONLY);
    if (mapped_data) {
        memcpy(dst, mapped_data, read_size);
        glUnmapBuffer(gltype);
    }
    else {
        ret = GetGLError();
    }
    glBindBuffer(gltype, 0);

    return ret;
}

Result GraphicsInterfaceOpenGL::writeBuffer(void *dst_buf, const void *src, size_t write_size, BufferType type)
{
    GLuint buf = (GLuint)(size_t)dst_buf;
    GLenum gltype = GetGLBufferType(type);

    Result ret = Result::OK;
    glBindBuffer(gltype, buf);
    void *mapped_data = glMapBuffer(gltype, GL_WRITE_ONLY);
    if (mapped_data) {
        memcpy(mapped_data, src, write_size);
        glUnmapBuffer(gltype);
    }
    else {
        ret = GetGLError();
    }
    glBindBuffer(gltype, 0);

    return ret;
}

} // namespace gi
#endif // giSupportOpenGL
