#include "audio.h"
#include "platform.h"

#include <algorithm>
#include <cmath>

#define STB_VORBIS_NO_PUSHDATA_API
#define STB_VORBIS_HEADER_ONLY
#include "ext/stb_vorbis.c"

namespace {
constexpr int kRate = 44100;
// Ganancia relativa de cada efecto (los más frecuentes, más discretos).
constexpr float kSfxGain[] = {0.32f, 0.8f, 0.7f, 0.9f, 0.85f, 0.9f};
} // namespace

bool Audio::Load(const std::string& path, Clip* clip)
{
  int channels = 0, rate = 0;
  short* data = nullptr;
  std::string file;
  if (!Platform::ReadWholeFile(path, &file))
    return false;
  const int frames = stb_vorbis_decode_memory(reinterpret_cast<const unsigned char*>(file.data()),
                                              static_cast<int>(file.size()), &channels, &rate, &data);
  if (frames <= 0 || !data)
  {
    SDL_Log("No se pudo cargar el audio %s", path.c_str());
    return false;
  }
  clip->pcm.resize(static_cast<size_t>(frames) * 2);
  for (int i = 0; i < frames; i++)
  {
    clip->pcm[i * 2] = data[i * channels];
    clip->pcm[i * 2 + 1] = data[i * channels + (channels > 1 ? 1 : 0)];
  }
  free(data);
  return true;
}

bool Audio::Init(const std::string& assets_dir)
{
  const std::string dir = assets_dir + "/audio/";
  const char* names[] = {"move.ogg", "select.ogg", "cancel.ogg", "start.ogg", "achievement.ogg", "close.ogg"};
  bool ok = Load(dir + "music.ogg", &m_music);
  for (int i = 0; i < static_cast<int>(Sfx::Count); i++)
    ok &= Load(dir + names[i], &m_sfx[i]);

  SDL_AudioSpec want = {}, have = {};
  want.freq = kRate;
  want.format = AUDIO_F32SYS;
  want.channels = 2;
  want.samples = 1024;
  want.callback = &Audio::Callback;
  want.userdata = this;
  m_dev = SDL_OpenAudioDevice(nullptr, 0, &want, &have, 0);
  if (!m_dev)
  {
    SDL_Log("Sin audio para la interfaz: %s", SDL_GetError());
    return false;
  }
  SDL_PauseAudioDevice(m_dev, 0);
  return ok;
}

void Audio::Shutdown()
{
  if (m_dev)
    SDL_CloseAudioDevice(m_dev);
  m_dev = 0;
}

void Audio::Play(Sfx s)
{
  const int i = static_cast<int>(s);
  if (!m_dev || m_sfx[i].pcm.empty())
    return;
  std::lock_guard<std::mutex> lock(m_mutex);
  // Un mismo efecto no se acumula: al repetirlo (p. ej. moverse rápido) reinicia el anterior.
  for (Voice& v : m_voices)
    if (v.clip == &m_sfx[i])
    {
      v.pos = 0;
      return;
    }
  m_voices.push_back({&m_sfx[i], 0, kSfxGain[i]});
}

void Audio::SetMusicLevel(float level) { m_music_target = level; }

void Audio::SetVolumes(int music_percent, int sfx_percent)
{
  // Curva perceptual: el 50 % suena a "mitad".
  auto curve = [](int p) { return std::pow(std::clamp(p, 0, 100) / 100.0f, 1.7f); };
  m_music_volume = curve(music_percent);
  m_sfx_volume = curve(sfx_percent);
}

void Audio::Callback(void* user, Uint8* stream, int len)
{
  static_cast<Audio*>(user)->Mix(reinterpret_cast<float*>(stream), len / static_cast<int>(sizeof(float) * 2));
}

void Audio::Mix(float* out, int frames)
{
  const float target = m_music_target;
  const float mvol = m_music_volume, svol = m_sfx_volume;
  // Fundido de ~1,2 s entre niveles de música.
  const float step = 1.0f / (kRate * 1.2f);
  const size_t music_frames = m_music.pcm.size() / 2;

  for (int i = 0; i < frames; i++)
  {
    if (m_music_gain < target)
      m_music_gain = std::min(target, m_music_gain + step);
    else if (m_music_gain > target)
      m_music_gain = std::max(target, m_music_gain - step);
    float l = 0, r = 0;
    if (music_frames)
    {
      const float g = m_music_gain * mvol / 32768.0f;
      l = m_music.pcm[m_music_pos * 2] * g;
      r = m_music.pcm[m_music_pos * 2 + 1] * g;
      if (++m_music_pos >= music_frames)
        m_music_pos = 0; // bucle continuo (la música se generó para empalmar sin cortes)
    }
    out[i * 2] = l;
    out[i * 2 + 1] = r;
  }

  std::lock_guard<std::mutex> lock(m_mutex);
  for (Voice& v : m_voices)
  {
    const size_t n = v.clip->pcm.size() / 2;
    const float g = v.gain * svol / 32768.0f;
    for (int i = 0; i < frames && v.pos < n; i++, v.pos++)
    {
      out[i * 2] += v.clip->pcm[v.pos * 2] * g;
      out[i * 2 + 1] += v.clip->pcm[v.pos * 2 + 1] * g;
    }
  }
  m_voices.erase(std::remove_if(m_voices.begin(), m_voices.end(),
                                [](const Voice& v) { return v.pos >= v.clip->pcm.size() / 2; }),
                 m_voices.end());
  for (int i = 0; i < frames * 2; i++)
    out[i] = std::clamp(out[i], -1.0f, 1.0f);
}
