// Música y efectos de la interfaz del launcher. Usa su propio dispositivo de audio (independiente
// del del emulador), mezclando en un callback de SDL.
#pragma once
#include <SDL.h>
#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

enum class Sfx
{
  Move,
  Select,
  Cancel,
  Start,
  Achievement,
  Close,
  Count
};

class Audio
{
public:
  bool Init(const std::string& assets_dir);
  void Shutdown();

  void Play(Sfx s);
  // Nivel al que tiende la música (0..1); el cambio se hace con un fundido suave.
  void SetMusicLevel(float level);
  void SetVolumes(int music_percent, int sfx_percent);

private:
  struct Clip
  {
    std::vector<int16_t> pcm; // estéreo intercalado, 44,1 kHz
  };
  struct Voice
  {
    const Clip* clip;
    size_t pos;
    float gain;
  };

  static void Callback(void* user, Uint8* stream, int len);
  void Mix(float* out, int frames);
  bool Load(const std::string& path, Clip* clip);

  SDL_AudioDeviceID m_dev = 0;
  Clip m_music;
  Clip m_sfx[static_cast<int>(Sfx::Count)];
  size_t m_music_pos = 0;
  float m_music_gain = 0.0f; // nivel actual (fundido)
  std::atomic<float> m_music_target{1.0f};
  std::atomic<float> m_music_volume{0.7f};
  std::atomic<float> m_sfx_volume{0.8f};
  std::mutex m_mutex; // protege m_voices
  std::vector<Voice> m_voices;
};
