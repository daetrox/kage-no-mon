// Frontend libretro mínimo: carga SwanStation, lo alimenta de vídeo/audio/input y expone la
// imagen resultante como una textura OpenGL que el launcher dibuja como quiera.
#pragma once
#include "config.h"
#include "gl.h"

#include <SDL.h>
#include <cstdint>
#include <functional>
#include <string>

struct EmuInput
{
  uint16_t buttons = 0;           // bits RETRO_DEVICE_ID_JOYPAD_*
  int16_t analog[2][2] = {};      // [stick izq/der][x/y]
};

class Emulator
{
public:
  bool Start(const Config& cfg, SDL_Window* window, SDL_GLContext ui_context, std::string* error);
  // Parche 16:9 del propio juego para un número de serie (nullptr si no hay).
  static const char* WidescreenPatch(const std::string& serial);
  void Stop();
  bool IsRunning() const { return m_running; }

  // Ejecuta los frames que tocan según el tiempo real y el nivel del buffer de audio.
  void Tick(const EmuInput& input);
  void SetPaused(bool paused);

  // Se invoca tras cada frame emulado (lo usan los logros para leer la RAM).
  std::function<void()> on_frame;
  const uint8_t* SystemRam(size_t* size) const;

  bool SaveState(const std::string& path);
  bool LoadState(const std::string& path);
  void SetVolume(int volume) { m_volume = volume; }
  void SetRumbleTarget(SDL_GameController* pad) { m_pad = pad; }

  // Imagen actual del juego.
  GLuint Texture() const { return m_hw ? m_hw_texture : m_sw_texture; }
  int FrameWidth() const { return m_frame_w; }
  int FrameHeight() const { return m_frame_h; }
  // Tamaño total de la textura (la imagen ocupa sólo FrameWidth x FrameHeight).
  int TextureWidth() const { return m_hw ? m_hw_tex_w : m_sw_tex_w; }
  int TextureHeight() const { return m_hw ? m_hw_tex_h : m_sw_tex_h; }
  bool FlippedY() const { return m_hw; } // en HW la imagen viene con origen abajo-izquierda
  float AspectRatio() const { return m_aspect; }
  bool HasFrame() const { return m_frame_w > 0; }
  double TargetFps() const { return m_fps; }
  uint64_t FramesRun() const { return m_frames_run; }

  // Callbacks libretro (estáticos, usan la instancia global).
  struct Impl;

private:
  friend struct Impl;
  bool LoadCore(std::string* error);
  void BuildOptions(const Config& cfg);
  bool SetupHardwareContext(std::string* error);
  void ResizeHardwareFramebuffer(int w, int h);
  void RunOneFrame();
  void MakeCoreCurrent();
  void MakeUiCurrent();

  void* m_lib = nullptr;
  bool m_core_initialized = false;
  bool m_running = false;
  bool m_paused = false;

  SDL_Window* m_window = nullptr;
  SDL_GLContext m_ui_ctx = nullptr;
  SDL_GLContext m_core_ctx = nullptr;

  // Renderizado por hardware (OpenGL del core).
  bool m_hw = false;
  GLuint m_hw_fbo = 0, m_hw_texture = 0, m_hw_depth = 0;
  int m_hw_tex_w = 0, m_hw_tex_h = 0;
  GLsync m_fence = nullptr;

  // Renderizado por software.
  GLuint m_sw_texture = 0;
  int m_sw_tex_w = 0, m_sw_tex_h = 0;
  unsigned m_pixel_format = 0;

  int m_frame_w = 0, m_frame_h = 0;
  float m_aspect = 4.0f / 3.0f;
  double m_fps = 60.0;
  double m_sample_rate = 44100.0;
  double m_time_accum = 0.0;
  uint64_t m_last_counter = 0;

  SDL_AudioDeviceID m_audio = 0;
  int m_volume = 100;
  SDL_GameController* m_pad = nullptr;

  EmuInput m_input;
  uint64_t m_frames_run = 0;
  std::string m_system_dir;
  std::string m_game_path;
};
