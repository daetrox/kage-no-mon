// Cargador mínimo de funciones OpenGL 3.3 Core usando SDL_GL_GetProcAddress.
#pragma once
#include <GL/glcorearb.h>

#define TENCHU_GL_FUNCS(X)                                                                         \
  X(PFNGLCLEARPROC, glClear)                                                                       \
  X(PFNGLCLEARCOLORPROC, glClearColor)                                                             \
  X(PFNGLVIEWPORTPROC, glViewport)                                                                 \
  X(PFNGLENABLEPROC, glEnable)                                                                     \
  X(PFNGLDISABLEPROC, glDisable)                                                                   \
  X(PFNGLBLENDFUNCPROC, glBlendFunc)                                                               \
  X(PFNGLBLENDFUNCSEPARATEPROC, glBlendFuncSeparate)                                               \
  X(PFNGLGETERRORPROC, glGetError)                                                                 \
  X(PFNGLGETSTRINGPROC, glGetString)                                                               \
  X(PFNGLGETINTEGERVPROC, glGetIntegerv)                                                           \
  X(PFNGLFLUSHPROC, glFlush)                                                                       \
  X(PFNGLFINISHPROC, glFinish)                                                                     \
  X(PFNGLREADPIXELSPROC, glReadPixels)                                                             \
  X(PFNGLPIXELSTOREIPROC, glPixelStorei)                                                           \
  X(PFNGLGENTEXTURESPROC, glGenTextures)                                                           \
  X(PFNGLDELETETEXTURESPROC, glDeleteTextures)                                                     \
  X(PFNGLBINDTEXTUREPROC, glBindTexture)                                                           \
  X(PFNGLACTIVETEXTUREPROC, glActiveTexture)                                                       \
  X(PFNGLTEXIMAGE2DPROC, glTexImage2D)                                                             \
  X(PFNGLTEXSUBIMAGE2DPROC, glTexSubImage2D)                                                       \
  X(PFNGLTEXPARAMETERIPROC, glTexParameteri)                                                       \
  X(PFNGLGENFRAMEBUFFERSPROC, glGenFramebuffers)                                                   \
  X(PFNGLDELETEFRAMEBUFFERSPROC, glDeleteFramebuffers)                                             \
  X(PFNGLBINDFRAMEBUFFERPROC, glBindFramebuffer)                                                   \
  X(PFNGLFRAMEBUFFERTEXTURE2DPROC, glFramebufferTexture2D)                                         \
  X(PFNGLCHECKFRAMEBUFFERSTATUSPROC, glCheckFramebufferStatus)                                     \
  X(PFNGLGENRENDERBUFFERSPROC, glGenRenderbuffers)                                                 \
  X(PFNGLDELETERENDERBUFFERSPROC, glDeleteRenderbuffers)                                           \
  X(PFNGLBINDRENDERBUFFERPROC, glBindRenderbuffer)                                                 \
  X(PFNGLRENDERBUFFERSTORAGEPROC, glRenderbufferStorage)                                           \
  X(PFNGLFRAMEBUFFERRENDERBUFFERPROC, glFramebufferRenderbuffer)                                   \
  X(PFNGLCREATESHADERPROC, glCreateShader)                                                         \
  X(PFNGLDELETESHADERPROC, glDeleteShader)                                                         \
  X(PFNGLSHADERSOURCEPROC, glShaderSource)                                                         \
  X(PFNGLCOMPILESHADERPROC, glCompileShader)                                                       \
  X(PFNGLGETSHADERIVPROC, glGetShaderiv)                                                           \
  X(PFNGLGETSHADERINFOLOGPROC, glGetShaderInfoLog)                                                 \
  X(PFNGLCREATEPROGRAMPROC, glCreateProgram)                                                       \
  X(PFNGLDELETEPROGRAMPROC, glDeleteProgram)                                                       \
  X(PFNGLATTACHSHADERPROC, glAttachShader)                                                         \
  X(PFNGLLINKPROGRAMPROC, glLinkProgram)                                                           \
  X(PFNGLGETPROGRAMIVPROC, glGetProgramiv)                                                         \
  X(PFNGLGETPROGRAMINFOLOGPROC, glGetProgramInfoLog)                                               \
  X(PFNGLUSEPROGRAMPROC, glUseProgram)                                                             \
  X(PFNGLGETUNIFORMLOCATIONPROC, glGetUniformLocation)                                             \
  X(PFNGLUNIFORM1IPROC, glUniform1i)                                                               \
  X(PFNGLUNIFORM1FPROC, glUniform1f)                                                               \
  X(PFNGLUNIFORM2FPROC, glUniform2f)                                                               \
  X(PFNGLUNIFORM4FPROC, glUniform4f)                                                               \
  X(PFNGLGENVERTEXARRAYSPROC, glGenVertexArrays)                                                   \
  X(PFNGLDELETEVERTEXARRAYSPROC, glDeleteVertexArrays)                                             \
  X(PFNGLBINDVERTEXARRAYPROC, glBindVertexArray)                                                   \
  X(PFNGLGENBUFFERSPROC, glGenBuffers)                                                             \
  X(PFNGLDELETEBUFFERSPROC, glDeleteBuffers)                                                       \
  X(PFNGLBINDBUFFERPROC, glBindBuffer)                                                             \
  X(PFNGLBUFFERDATAPROC, glBufferData)                                                             \
  X(PFNGLENABLEVERTEXATTRIBARRAYPROC, glEnableVertexAttribArray)                                   \
  X(PFNGLVERTEXATTRIBPOINTERPROC, glVertexAttribPointer)                                           \
  X(PFNGLDRAWARRAYSPROC, glDrawArrays)                                                             \
  X(PFNGLFENCESYNCPROC, glFenceSync)                                                               \
  X(PFNGLWAITSYNCPROC, glWaitSync)                                                                 \
  X(PFNGLDELETESYNCPROC, glDeleteSync)                                                             \
  X(PFNGLGENERATEMIPMAPPROC, glGenerateMipmap)                                                     \
  X(PFNGLSCISSORPROC, glScissor)

// Los punteros viven en un namespace para no chocar con los símbolos de libGL.
namespace tgl {
#define TENCHU_GL_DECLARE(type, name) extern type name;
TENCHU_GL_FUNCS(TENCHU_GL_DECLARE)
#undef TENCHU_GL_DECLARE

bool LoadGLFunctions();
} // namespace tgl

using namespace tgl;
