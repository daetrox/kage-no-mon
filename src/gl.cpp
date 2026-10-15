#include "gl.h"
#include <SDL.h>

namespace tgl {
#define TENCHU_GL_DEFINE(type, name) type name = nullptr;
TENCHU_GL_FUNCS(TENCHU_GL_DEFINE)
#undef TENCHU_GL_DEFINE

bool LoadGLFunctions()
{
  bool ok = true;
#define TENCHU_GL_LOAD(type, name)                                                                 \
  name = reinterpret_cast<type>(SDL_GL_GetProcAddress(#name));                                     \
  if (!name)                                                                                       \
  {                                                                                                \
    SDL_Log("Falta la función OpenGL %s", #name);                                                 \
    ok = false;                                                                                    \
  }
  TENCHU_GL_FUNCS(TENCHU_GL_LOAD)
#undef TENCHU_GL_LOAD
  return ok;
}
} // namespace tgl
