#include "emulator.h"
#include "libretro.h"

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dlfcn.h>
#include <fstream>
#include <map>
#include <sstream>
#include <vector>

std::string ReadDiscSerialCached(const std::string& path);

// ¿El usuario eligió una BIOS propia (y sigue existiendo)? Si no, se usa OpenBIOS.
static bool UsesCustomBios(const Config& cfg)
{
  return !cfg.bios_file.empty() && Paths::FileExists(Paths::BiosDir() + "/" + cfg.bios_file);
}

// --- Símbolos del core -----------------------------------------------------------------------

namespace {

struct CoreApi
{
  void (*retro_init)();
  void (*retro_deinit)();
  void (*retro_set_environment)(retro_environment_t);
  void (*retro_set_video_refresh)(retro_video_refresh_t);
  void (*retro_set_audio_sample)(retro_audio_sample_t);
  void (*retro_set_audio_sample_batch)(retro_audio_sample_batch_t);
  void (*retro_set_input_poll)(retro_input_poll_t);
  void (*retro_set_input_state)(retro_input_state_t);
  void (*retro_get_system_av_info)(retro_system_av_info*);
  void (*retro_set_controller_port_device)(unsigned, unsigned);
  void (*retro_cheat_reset)();
  void (*retro_cheat_set)(unsigned, bool, const char*);
  bool (*retro_load_game)(const retro_game_info*);
  void (*retro_unload_game)();
  void (*retro_run)();
  size_t (*retro_serialize_size)();
  bool (*retro_serialize)(void*, size_t);
  bool (*retro_unserialize)(const void*, size_t);
  void* (*retro_get_memory_data)(unsigned);
  size_t (*retro_get_memory_size)(unsigned);
};

CoreApi g_core;
Emulator* g_emu = nullptr;
retro_hw_render_callback g_hw_cb = {};
bool g_hw_requested = false;
std::map<std::string, std::string> g_options;
std::vector<int16_t> g_audio_scratch;

// Dispositivo "mando DualShock" definido por SwanStation.
constexpr unsigned kDeviceDualShock = RETRO_DEVICE_SUBCLASS(RETRO_DEVICE_ANALOG, 0);

void CoreLog(enum retro_log_level level, const char* fmt, ...)
{
  static const bool debug = std::getenv("TENSHU_DEBUG") != nullptr;
  if (level < (debug ? RETRO_LOG_INFO : RETRO_LOG_WARN))
    return;
  char buf[2048];
  va_list args;
  va_start(args, fmt);
  std::vsnprintf(buf, sizeof(buf), fmt, args);
  va_end(args);
  SDL_Log("[core] %s", buf);
}

} // namespace

struct Emulator::Impl
{
  static bool Environment(unsigned cmd, void* data)
  {
    Emulator& e = *g_emu;
    switch (cmd)
    {
      case RETRO_ENVIRONMENT_GET_LOG_INTERFACE:
        static_cast<retro_log_callback*>(data)->log = CoreLog;
        return true;

      case RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY:
        *static_cast<const char**>(data) = e.m_system_dir.c_str();
        return true;

      case RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY:
      {
        static std::string dir;
        dir = Paths::SavesDir();
        *static_cast<const char**>(data) = dir.c_str();
        return true;
      }

      case RETRO_ENVIRONMENT_GET_CORE_ASSETS_DIRECTORY:
      {
        static std::string dir;
        dir = Paths::DataDir();
        *static_cast<const char**>(data) = dir.c_str();
        return true;
      }

      case RETRO_ENVIRONMENT_GET_VARIABLE:
      {
        auto* var = static_cast<retro_variable*>(data);
        auto it = g_options.find(var->key);
        if (it == g_options.end())
          return false;
        var->value = it->second.c_str();
        return true;
      }

      case RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE:
        *static_cast<bool*>(data) = false;
        return true;

      case RETRO_ENVIRONMENT_GET_CORE_OPTIONS_VERSION:
        *static_cast<unsigned*>(data) = 0;
        return true;

      case RETRO_ENVIRONMENT_SET_VARIABLES:
      case RETRO_ENVIRONMENT_SET_CORE_OPTIONS:
      case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_INTL:
      case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_V2:
      case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_V2_INTL:
      case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY:
      case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_UPDATE_DISPLAY_CALLBACK:
      case RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS:
      case RETRO_ENVIRONMENT_SET_CONTROLLER_INFO:
      case RETRO_ENVIRONMENT_SET_SUPPORT_NO_GAME:
      case RETRO_ENVIRONMENT_SET_DISK_CONTROL_INTERFACE:
      case RETRO_ENVIRONMENT_SET_DISK_CONTROL_EXT_INTERFACE:
        return true;

      // Mensajes en pantalla del emulador: se descartan para no dejar rastro del emulador.
      case RETRO_ENVIRONMENT_SET_MESSAGE:
      case RETRO_ENVIRONMENT_SET_MESSAGE_EXT:
        return true;

      case RETRO_ENVIRONMENT_GET_INPUT_BITMASKS:
        return true;

      case RETRO_ENVIRONMENT_GET_AUDIO_VIDEO_ENABLE:
        if (data)
          *static_cast<int*>(data) = 3;
        return true;

      case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT:
      {
        const auto fmt = *static_cast<retro_pixel_format*>(data);
        if (fmt != RETRO_PIXEL_FORMAT_XRGB8888 && fmt != RETRO_PIXEL_FORMAT_RGB565)
          return false;
        e.m_pixel_format = fmt;
        return true;
      }

      case RETRO_ENVIRONMENT_GET_PREFERRED_HW_RENDER:
        *static_cast<unsigned*>(data) = RETRO_HW_CONTEXT_OPENGL_CORE;
        return true;

      case RETRO_ENVIRONMENT_SET_HW_RENDER:
      {
        auto* cb = static_cast<retro_hw_render_callback*>(data);
        // Sólo ofrecemos OpenGL 3.x Core (o compatibilidad, que en Mesa también es 3.x).
        if (cb->context_type != RETRO_HW_CONTEXT_OPENGL_CORE && cb->context_type != RETRO_HW_CONTEXT_OPENGL)
          return false;
        cb->get_current_framebuffer = []() -> uintptr_t { return g_emu->m_hw_fbo; };
        cb->get_proc_address = [](const char* sym) -> retro_proc_address_t {
          return reinterpret_cast<retro_proc_address_t>(SDL_GL_GetProcAddress(sym));
        };
        g_hw_cb = *cb;
        g_hw_requested = true;
        return true;
      }

      case RETRO_ENVIRONMENT_SET_GEOMETRY:
      {
        const auto* geom = static_cast<const retro_game_geometry*>(data);
        if (geom->aspect_ratio > 0.0f)
          e.m_aspect = geom->aspect_ratio;
        return true;
      }

      case RETRO_ENVIRONMENT_SET_SYSTEM_AV_INFO:
      {
        const auto* av = static_cast<const retro_system_av_info*>(data);
        if (av->geometry.aspect_ratio > 0.0f)
          e.m_aspect = av->geometry.aspect_ratio;
        e.m_fps = av->timing.fps;
        if (e.m_hw && e.m_hw_fbo)
          e.ResizeHardwareFramebuffer(static_cast<int>(av->geometry.max_width),
                                      static_cast<int>(av->geometry.max_height));
        return true;
      }

      case RETRO_ENVIRONMENT_GET_RUMBLE_INTERFACE:
        static_cast<retro_rumble_interface*>(data)->set_rumble_state = [](unsigned port, retro_rumble_effect effect,
                                                                            uint16_t strength) -> bool {
          static uint16_t strong = 0, weak = 0;
          if (port != 0 || !g_emu->m_pad)
            return false;
          (effect == RETRO_RUMBLE_STRONG ? strong : weak) = strength;
          SDL_GameControllerRumble(g_emu->m_pad, strong, weak, 200);
          return true;
        };
        return true;

      default:
        return false;
    }
  }

  static void VideoRefresh(const void* data, unsigned width, unsigned height, size_t pitch)
  {
    Emulator& e = *g_emu;
    if (!data)
      return; // frame duplicado: se mantiene la imagen anterior

    e.m_frame_w = static_cast<int>(width);
    e.m_frame_h = static_cast<int>(height);
    if (data == RETRO_HW_FRAME_BUFFER_VALID)
      return;

    // Software: subir el framebuffer a una textura propia (contexto del launcher).
    if (!e.m_sw_texture)
      glGenTextures(1, &e.m_sw_texture);
    glBindTexture(GL_TEXTURE_2D, e.m_sw_texture);
    const bool rgb565 = (e.m_pixel_format == RETRO_PIXEL_FORMAT_RGB565);
    const int bpp = rgb565 ? 2 : 4;
    if (static_cast<int>(width) > e.m_sw_tex_w || static_cast<int>(height) > e.m_sw_tex_h)
    {
      e.m_sw_tex_w = 1024;
      e.m_sw_tex_h = 1024;
      while (e.m_sw_tex_w < static_cast<int>(width)) e.m_sw_tex_w *= 2;
      while (e.m_sw_tex_h < static_cast<int>(height)) e.m_sw_tex_h *= 2;
      glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, e.m_sw_tex_w, e.m_sw_tex_h, 0, GL_BGRA,
                   GL_UNSIGNED_INT_8_8_8_8_REV, nullptr);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    }
    glPixelStorei(GL_UNPACK_ROW_LENGTH, static_cast<GLint>(pitch / bpp));
    if (rgb565)
      glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width, height, GL_RGB, GL_UNSIGNED_SHORT_5_6_5, data);
    else
      glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width, height, GL_BGRA, GL_UNSIGNED_INT_8_8_8_8_REV, data);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
  }

  static size_t AudioBatch(const int16_t* data, size_t frames)
  {
    Emulator& e = *g_emu;
    if (!e.m_audio)
      return frames;
    const size_t samples = frames * 2;
    g_audio_scratch.resize(samples);
    const int vol = e.m_volume;
    for (size_t i = 0; i < samples; i++)
      g_audio_scratch[i] = static_cast<int16_t>((static_cast<int>(data[i]) * vol) / 100);
    SDL_QueueAudio(e.m_audio, g_audio_scratch.data(), static_cast<Uint32>(samples * sizeof(int16_t)));
    return frames;
  }

  static void AudioSample(int16_t left, int16_t right)
  {
    const int16_t s[2] = {left, right};
    AudioBatch(s, 1);
  }

  static void InputPoll() {}

  static int16_t InputState(unsigned port, unsigned device, unsigned index, unsigned id)
  {
    if (port != 0)
      return 0;
    const EmuInput& in = g_emu->m_input;
    switch (device & RETRO_DEVICE_MASK)
    {
      case RETRO_DEVICE_JOYPAD:
        if (id == RETRO_DEVICE_ID_JOYPAD_MASK)
          return static_cast<int16_t>(in.buttons);
        return (in.buttons >> id) & 1;

      case RETRO_DEVICE_ANALOG:
        if (index == RETRO_DEVICE_INDEX_ANALOG_BUTTON)
          return ((in.buttons >> id) & 1) ? 0x7fff : 0;
        if (index <= RETRO_DEVICE_INDEX_ANALOG_RIGHT && id <= RETRO_DEVICE_ID_ANALOG_Y)
          return in.analog[index][id];
        return 0;

      default:
        return 0;
    }
  }
};

// --- Ciclo de vida -----------------------------------------------------------------------------

// Parches 16:9 de la base de datos comunitaria de DuckStation (chtdb, carpeta patches/).
// Autores: El_Patas & PeterDelta. Los códigos A7 (exclusivos de DuckStation) se expresan como el
// par equivalente D0 (si es igual a) + 80 (escribir 16 bits), que SwanStation sí entiende.
const char* Emulator::WidescreenPatch(const std::string& serial)
{
  struct Patch
  {
    const char* serial;
    const char* codes;
  };
  static const Patch patches[] = {
    // Tenchu 2 (España)
    {"SLES-02465", "D00CCE90 1000+800CCE90 0C00+D00CFF78 1000+800CFF78 0C00"},
    // Tenchu 2 (Europa)
    {"SLES-02452", "D00CCEA0 1000+800CCEA0 0C00+D00CFF8C 1000+800CFF8C 0C00"},
  };
  for (const Patch& p : patches)
    if (serial == p.serial)
      return p.codes;
  return nullptr;
}

std::string ReadDiscSerialCached(const std::string& path)
{
  static std::string last_path, last_serial;
  if (path != last_path)
  {
    last_path = path;
    last_serial = Paths::ReadDiscSerial(path);
  }
  return last_serial;
}

bool Emulator::LoadCore(std::string* error)
{
  if (m_lib)
    return true;

  const std::string path = Paths::CorePath();
  m_lib = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
  if (!m_lib)
  {
    *error = "No se pudo cargar el núcleo de emulación:\n" + std::string(dlerror());
    return false;
  }

#define LOAD_SYM(name)                                                                             \
  g_core.name = reinterpret_cast<decltype(g_core.name)>(dlsym(m_lib, #name));                     \
  if (!g_core.name)                                                                                \
  {                                                                                                \
    *error = "El núcleo no exporta " #name;                                                        \
    return false;                                                                                  \
  }
  LOAD_SYM(retro_init)
  LOAD_SYM(retro_deinit)
  LOAD_SYM(retro_set_environment)
  LOAD_SYM(retro_set_video_refresh)
  LOAD_SYM(retro_set_audio_sample)
  LOAD_SYM(retro_set_audio_sample_batch)
  LOAD_SYM(retro_set_input_poll)
  LOAD_SYM(retro_set_input_state)
  LOAD_SYM(retro_get_system_av_info)
  LOAD_SYM(retro_set_controller_port_device)
  LOAD_SYM(retro_cheat_reset)
  LOAD_SYM(retro_cheat_set)
  LOAD_SYM(retro_load_game)
  LOAD_SYM(retro_unload_game)
  LOAD_SYM(retro_run)
  LOAD_SYM(retro_serialize_size)
  LOAD_SYM(retro_serialize)
  LOAD_SYM(retro_unserialize)
  LOAD_SYM(retro_get_memory_data)
  LOAD_SYM(retro_get_memory_size)
#undef LOAD_SYM
  return true;
}

void Emulator::BuildOptions(const Config& cfg)
{
  g_options.clear();
  auto opt = [](const char* key, const std::string& value) { g_options[key] = value; };
  auto flag = [](bool b) { return std::string(b ? "true" : "false"); };

  // BIOS: por defecto OpenBIOS (libre, integrada en el núcleo). Se usa cuando la carpeta "system"
  // no contiene ninguna BIOS válida; si el usuario elige una de bios/, se usa para cualquier región.
  // de la región correcta identificándola por su hash.
  const std::string bios = UsesCustomBios(cfg) ? cfg.bios_file : "openbios.bin";
  opt("swanstation_BIOS_PathNTSCJ", bios);
  opt("swanstation_BIOS_PathNTSCU", bios);
  opt("swanstation_BIOS_PathPAL", bios);
  opt("swanstation_BIOS_PatchFastBoot", flag(cfg.fast_boot));

  opt("swanstation_GPU_Renderer", cfg.hardware_renderer ? "OpenGL" : "Software");
  opt("swanstation_GPU_ResolutionScale", std::to_string(cfg.hardware_renderer ? cfg.resolution_scale : 1));
  opt("swanstation_GPU_PGXPEnable", flag(cfg.hardware_renderer && cfg.pgxp));
  opt("swanstation_GPU_TextureFilter", cfg.hardware_renderer ? cfg.texture_filter : "Nearest");
  opt("swanstation_GPU_PGXPCulling", "true");
  opt("swanstation_GPU_PGXPTextureCorrection", "true");
  opt("swanstation_GPU_TrueColor", flag(cfg.hardware_renderer));
  // Nunca el truco panorámico del emulador: en Tenchu 2 descoloca el HUD (parte se dibuja en 3D
  // y parte en 2D). La pantalla ancha se consigue con un parche del propio juego (ver más abajo).
  const bool ws = cfg.widescreen && WidescreenPatch(ReadDiscSerialCached(cfg.game_path)) != nullptr;
  opt("swanstation_GPU_WidescreenHack", "false");
  opt("swanstation_Display_AspectRatio", ws ? "16:9" : "4:3");
  opt("swanstation_Display_CropMode", "Borders");
  opt("swanstation_Display_ShowOSDMessages", "false");

  opt("swanstation_Controller1_ForceAnalog", "true");
  opt("swanstation_Controller_EnableRumble", "true");
  opt("swanstation_MemoryCards_Card1Type", "PerGameTitle");
  opt("swanstation_CPU_ExecutionMode", "Recompiler");
  opt("swanstation_Logging_LogLevel", std::getenv("TENSHU_DEBUG") ? "Info" : "Warning");

  // Depuración: TENSHU_CORE_OPTS="clave=valor;clave=valor" sobrescribe opciones del núcleo.
  if (const char* extra = std::getenv("TENSHU_CORE_OPTS"))
  {
    std::stringstream ss(extra);
    std::string item;
    while (std::getline(ss, item, ';'))
    {
      const size_t eq = item.find('=');
      if (eq != std::string::npos)
        g_options[item.substr(0, eq)] = item.substr(eq + 1);
    }
  }
}

bool Emulator::Start(const Config& cfg, SDL_Window* window, SDL_GLContext ui_context, std::string* error)
{
  if (m_running)
    return true;

  if (!Paths::FileExists(cfg.game_path))
  {
    *error = "No se encuentra la imagen del juego.\nSelecciónala en OPCIONES.";
    return false;
  }

  m_window = window;
  m_ui_ctx = ui_context;
  g_emu = this;

  Paths::MakeDirs(Paths::SavesDir());
  Paths::MakeDirs(Paths::StatesDir());
  // Con OpenBIOS, la carpeta "system" es una vacía: el núcleo no encuentra BIOS y usa la integrada.
  m_system_dir = UsesCustomBios(cfg) ? Paths::BiosDir() : Paths::DataDir() + "/system/openbios";
  Paths::MakeDirs(m_system_dir);

  if (!LoadCore(error))
    return false;

  BuildOptions(cfg);
  m_volume = cfg.volume;
  g_hw_requested = false;
  m_hw = false;
  m_frame_w = m_frame_h = 0;

  if (!m_core_initialized)
  {
    g_core.retro_set_environment(&Impl::Environment);
    g_core.retro_init();
    m_core_initialized = true;
  }
  g_core.retro_set_environment(&Impl::Environment);
  g_core.retro_set_video_refresh(&Impl::VideoRefresh);
  g_core.retro_set_audio_sample(&Impl::AudioSample);
  g_core.retro_set_audio_sample_batch(&Impl::AudioBatch);
  g_core.retro_set_input_poll(&Impl::InputPoll);
  g_core.retro_set_input_state(&Impl::InputState);

  m_game_path = cfg.game_path;
  retro_game_info info = {};
  info.path = m_game_path.c_str();
  if (!g_core.retro_load_game(&info))
  {
    *error = "El núcleo no pudo arrancar el juego.\nComprueba la imagen del disco y la BIOS.";
    return false;
  }

  g_core.retro_set_controller_port_device(0, kDeviceDualShock);

  // Parche panorámico nativo del juego (GameShark, aplicado por el núcleo en cada frame).
  g_core.retro_cheat_reset();
  if (cfg.widescreen)
    if (const char* patch = WidescreenPatch(ReadDiscSerialCached(cfg.game_path)))
      g_core.retro_cheat_set(0, true, patch);

  retro_system_av_info av = {};
  g_core.retro_get_system_av_info(&av);
  m_fps = av.timing.fps > 0 ? av.timing.fps : 60.0;
  m_sample_rate = av.timing.sample_rate > 0 ? av.timing.sample_rate : 44100.0;
  if (av.geometry.aspect_ratio > 0.0f)
    m_aspect = av.geometry.aspect_ratio;

  if (g_hw_requested)
  {
    if (!SetupHardwareContext(error))
    {
      g_core.retro_unload_game();
      return false;
    }
    ResizeHardwareFramebuffer(static_cast<int>(av.geometry.max_width), static_cast<int>(av.geometry.max_height));
    MakeCoreCurrent();
    g_hw_cb.context_reset();
    MakeUiCurrent();
  }

  SDL_AudioSpec want = {}, have = {};
  want.freq = static_cast<int>(m_sample_rate);
  want.format = AUDIO_S16SYS;
  want.channels = 2;
  want.samples = 1024;
  m_audio = SDL_OpenAudioDevice(nullptr, 0, &want, &have, 0);
  if (m_audio)
    SDL_PauseAudioDevice(m_audio, 0);

  m_time_accum = 0.0;
  m_last_counter = SDL_GetPerformanceCounter();
  m_paused = false;
  m_running = true;
  return true;
}

void Emulator::Stop()
{
  if (!m_running)
    return;

  if (m_audio)
  {
    SDL_CloseAudioDevice(m_audio);
    m_audio = 0;
  }

  if (m_hw)
  {
    // Igual que RetroArch: primero se avisa al core de que el contexto desaparece y después se
    // descarga el juego (al revés, el core intenta recrear la GPU sobre un sistema ya cerrado).
    MakeCoreCurrent();
    if (g_hw_cb.context_destroy)
      g_hw_cb.context_destroy();
    g_core.retro_unload_game();
    if (m_fence)
      glDeleteSync(m_fence);
    m_fence = nullptr;
    glDeleteFramebuffers(1, &m_hw_fbo);
    glDeleteRenderbuffers(1, &m_hw_depth);
    glDeleteTextures(1, &m_hw_texture);
    m_hw_fbo = m_hw_depth = m_hw_texture = 0;
    m_hw_tex_w = m_hw_tex_h = 0;
    MakeUiCurrent();
    SDL_GL_DeleteContext(m_core_ctx);
    m_core_ctx = nullptr;
  }
  else
  {
    g_core.retro_unload_game();
  }

  m_hw = false;
  m_running = false;
  m_frame_w = m_frame_h = 0;
}

bool Emulator::SetupHardwareContext(std::string* error)
{
  // Contexto propio del core, compartiendo objetos con el del launcher: así el estado GL del
  // emulador y el de la interfaz nunca se pisan, y la textura final es visible desde ambos.
  MakeUiCurrent();
  SDL_GL_SetAttribute(SDL_GL_SHARE_WITH_CURRENT_CONTEXT, 1);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
  m_core_ctx = SDL_GL_CreateContext(m_window);
  SDL_GL_SetAttribute(SDL_GL_SHARE_WITH_CURRENT_CONTEXT, 0);
  if (!m_core_ctx)
  {
    *error = std::string("No se pudo crear el contexto OpenGL del emulador:\n") + SDL_GetError();
    MakeUiCurrent();
    return false;
  }

  glGenFramebuffers(1, &m_hw_fbo);
  glGenTextures(1, &m_hw_texture);
  glGenRenderbuffers(1, &m_hw_depth);
  m_hw = true;
  MakeUiCurrent();
  return true;
}

void Emulator::ResizeHardwareFramebuffer(int w, int h)
{
  if (w <= m_hw_tex_w && h <= m_hw_tex_h)
    return;

  SDL_GLContext prev = SDL_GL_GetCurrentContext();
  MakeCoreCurrent();
  m_hw_tex_w = w;
  m_hw_tex_h = h;
  glBindTexture(GL_TEXTURE_2D, m_hw_texture);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glBindRenderbuffer(GL_RENDERBUFFER, m_hw_depth);
  glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, w, h);
  glBindFramebuffer(GL_FRAMEBUFFER, m_hw_fbo);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_hw_texture, 0);
  glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, m_hw_depth);
  if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
    SDL_Log("Framebuffer del emulador incompleto (%dx%d)", w, h);
  glClearColor(0, 0, 0, 1);
  glClear(GL_COLOR_BUFFER_BIT);
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  if (prev == m_ui_ctx)
    MakeUiCurrent();
}

void Emulator::MakeCoreCurrent()
{
  if (m_hw && SDL_GL_GetCurrentContext() != m_core_ctx)
    SDL_GL_MakeCurrent(m_window, m_core_ctx);
}

void Emulator::MakeUiCurrent()
{
  if (SDL_GL_GetCurrentContext() != m_ui_ctx)
  {
    SDL_GL_MakeCurrent(m_window, m_ui_ctx);
    // En EGL el intervalo de vsync se pierde al alternar contextos sobre la misma superficie.
    SDL_GL_SetSwapInterval(1);
  }
}

// --- Ejecución ---------------------------------------------------------------------------------

void Emulator::RunOneFrame()
{
  g_core.retro_run();
  m_frames_run++;
  if (on_frame)
    on_frame();
}

const uint8_t* Emulator::SystemRam(size_t* size) const
{
  if (!m_running)
    return nullptr;
  *size = g_core.retro_get_memory_size(RETRO_MEMORY_SYSTEM_RAM);
  return static_cast<const uint8_t*>(g_core.retro_get_memory_data(RETRO_MEMORY_SYSTEM_RAM));
}

void Emulator::SetPaused(bool paused)
{
  if (m_paused == paused)
    return;
  m_paused = paused;
  if (m_audio)
  {
    SDL_PauseAudioDevice(m_audio, paused ? 1 : 0);
    if (paused)
      SDL_ClearQueuedAudio(m_audio);
  }
  m_time_accum = 0.0;
  m_last_counter = SDL_GetPerformanceCounter();
}

void Emulator::Tick(const EmuInput& input)
{
  if (!m_running)
    return;

  const uint64_t now = SDL_GetPerformanceCounter();
  const double dt = static_cast<double>(now - m_last_counter) / static_cast<double>(SDL_GetPerformanceFrequency());
  m_last_counter = now;
  if (m_paused)
    return;

  m_input = input;
  m_time_accum += dt;

  // Corrección con el buffer de audio para evitar cortes o latencia creciente.
  const double frame_time = 1.0 / m_fps;
  if (m_audio)
  {
    // Objetivo ~80 ms en cola. SDL consume en bloques de ~23 ms, así que los márgenes son amplios
    // y la corrección es suave (±0,5 % de velocidad) para no alterar el ritmo del juego.
    const double queued = SDL_GetQueuedAudioSize(m_audio) / (4.0 * m_sample_rate);
    if (queued < 0.04)
      m_time_accum += dt * 0.005 + (queued < 0.01 ? frame_time * 0.5 : 0.0);
    else if (queued > 0.14)
      m_time_accum -= dt * 0.005 + (queued > 0.25 ? frame_time * 0.5 : 0.0);
  }
  if (m_time_accum > frame_time * 4)
    m_time_accum = frame_time * 4; // tras un tirón, no intentar recuperar todo de golpe

  int frames = 0;
  if (m_time_accum >= frame_time)
  {
    MakeCoreCurrent();
    while (m_time_accum >= frame_time && frames < 4)
    {
      RunOneFrame();
      m_time_accum -= frame_time;
      frames++;
    }
    if (m_hw)
    {
      if (m_fence)
        glDeleteSync(m_fence);
      m_fence = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
      glFlush();
    }
    MakeUiCurrent();
  }

  if (m_hw && m_fence)
  {
    glWaitSync(m_fence, 0, GL_TIMEOUT_IGNORED);
  }
}

// --- Estados guardados -------------------------------------------------------------------------

bool Emulator::SaveState(const std::string& path)
{
  if (!m_running)
    return false;
  MakeCoreCurrent();
  std::vector<uint8_t> buf(g_core.retro_serialize_size());
  const bool ok = !buf.empty() && g_core.retro_serialize(buf.data(), buf.size());
  MakeUiCurrent();
  if (!ok)
    return false;
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  out.write(reinterpret_cast<const char*>(buf.data()), static_cast<std::streamsize>(buf.size()));
  return static_cast<bool>(out);
}

bool Emulator::LoadState(const std::string& path)
{
  if (!m_running)
    return false;
  std::ifstream in(path, std::ios::binary);
  if (!in)
    return false;
  std::vector<uint8_t> buf((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  MakeCoreCurrent();
  const bool ok = !buf.empty() && g_core.retro_unserialize(buf.data(), buf.size());
  MakeUiCurrent();
  return ok;
}
