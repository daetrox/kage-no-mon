#include "gl.h"
#include <SDL.h>

namespace tgl {
#define TENSHU_GL_DEFINE(type, name) type name = nullptr;
TENSHU_GL_FUNCS(TENSHU_GL_DEFINE)
#undef TENSHU_GL_DEFINE

bool LoadGLFunctions()
{
  bool ok = true;
#define TENSHU_GL_LOAD(type, name)                                                                 \
  name = reinterpret_cast<type>(SDL_GL_GetProcAddress(#name));                                     \
  if (!name)                                                                                       \
  {                                                                                                \
    SDL_Log("Falta la función OpenGL %s", #name);                                                 \
    ok = false;                                                                                    \
  }
  TENSHU_GL_FUNCS(TENSHU_GL_LOAD)
#undef TENSHU_GL_LOAD
  return ok;
}
} // namespace tgl
