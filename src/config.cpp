#include "config.h"

#include <SDL_gamecontroller.h>
#include <SDL_scancode.h>

#include <algorithm>
#include <cstdlib>
#include <dirent.h>
#include <strings.h>
#include <fstream>
#include <sys/stat.h>
#include <unistd.h>

namespace Paths {

std::string ExeDir()
{
  static std::string dir;
  if (dir.empty())
  {
    char buf[4096];
    const ssize_t len = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (len > 0)
    {
      buf[len] = '\0';
      dir = buf;
      dir = dir.substr(0, dir.find_last_of('/'));
    }
    else
    {
      dir = ".";
    }
  }
  return dir;
}

std::string AssetsDir() { return ExeDir() + "/assets"; }
std::string CorePath() { return ExeDir() + "/core/swanstation_libretro.so"; }

static std::string Home()
{
  const char* home = std::getenv("HOME");
  return home ? home : ".";
}

std::string ConfigDir()
{
  const char* xdg = std::getenv("XDG_CONFIG_HOME");
  return (xdg && *xdg ? std::string(xdg) : Home() + "/.config") + "/tenshu2";
}

std::string DataDir()
{
  const char* xdg = std::getenv("XDG_DATA_HOME");
  return (xdg && *xdg ? std::string(xdg) : Home() + "/.local/share") + "/tenshu2";
}

std::string SavesDir() { return DataDir() + "/memcards"; }
std::string StatesDir() { return DataDir() + "/states"; }
// Carpeta con los archivos del usuario (bios/, game/). Dentro de un AppImage el programa vive en
// un sistema de archivos de sólo lectura, así que se usa la carpeta donde está el .AppImage.
std::string UserDir()
{
  if (const char* appimage = std::getenv("APPIMAGE"))
  {
    std::string p = appimage;
    const size_t s = p.find_last_of('/');
    if (s != std::string::npos)
      return p.substr(0, s);
  }
  return ExeDir();
}

std::string BiosDir() { return UserDir() + "/bios"; }
std::string GameDir() { return UserDir() + "/game"; }

std::vector<std::string> FindBiosFiles()
{
  std::vector<std::string> out;
  if (DIR* dp = opendir(BiosDir().c_str()))
  {
    while (dirent* e = readdir(dp))
    {
      struct stat st;
      const std::string full = BiosDir() + "/" + e->d_name;
      if (stat(full.c_str(), &st) == 0 && S_ISREG(st.st_mode) && st.st_size == 512 * 1024)
        out.push_back(e->d_name);
    }
    closedir(dp);
  }
  std::sort(out.begin(), out.end(), [](const std::string& a, const std::string& b) {
    return strcasecmp(a.c_str(), b.c_str()) < 0;
  });
  return out;
}

bool FileExists(const std::string& path)
{
  struct stat st;
  return !path.empty() && stat(path.c_str(), &st) == 0 && S_ISREG(st.st_mode);
}

std::string ReadDiscSerial(const std::string& image_path)
{
  // Para .cue se abre el primer .bin que referencia.
  std::string bin = image_path;
  const std::string lower = [&] {
    std::string l = image_path;
    for (char& c : l)
      c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
    return l;
  }();
  if (lower.size() > 4 && lower.compare(lower.size() - 4, 4, ".cue") == 0)
  {
    std::ifstream cue(image_path);
    std::string line;
    bin.clear();
    while (std::getline(cue, line))
    {
      const size_t a = line.find('"'), b = line.rfind('"');
      if (line.find("FILE") != std::string::npos && a != std::string::npos && b > a)
      {
        const std::string name = line.substr(a + 1, b - a - 1);
        const size_t slash = image_path.find_last_of('/');
        bin = (slash == std::string::npos ? std::string() : image_path.substr(0, slash + 1)) + name;
        break;
      }
    }
  }
  std::ifstream in(bin, std::ios::binary);
  if (!in)
    return {};
  // Busca "BOOT = cdrom:\XXXX_YYY.ZZ" en los primeros sectores (imagen en bruto de 2352 bytes).
  std::string sector(2352, '\0');
  for (int s = 16; s < 600; s++)
  {
    in.seekg(static_cast<std::streamoff>(s) * 2352);
    if (!in.read(&sector[0], 2352))
      break;
    const size_t boot = sector.find("BOOT");
    if (boot == std::string::npos)
      continue;
    const size_t colon = sector.find("cdrom:", boot);
    if (colon == std::string::npos || colon - boot > 16)
      continue;
    size_t p = colon + 6;
    while (p < sector.size() && (sector[p] == '\\' || sector[p] == ' '))
      p++;
    std::string id;
    for (; p < sector.size() && id.size() < 16; p++)
    {
      const char c = sector[p];
      if (c == ';' || c == '\r' || c == '\n' || c == ' ')
        break;
      if (c == '_')
        id += '-';
      else if (c != '.')
        id += static_cast<char>(toupper(static_cast<unsigned char>(c)));
    }
    return id;
  }
  return {};
}

bool CopyFile(const std::string& from, const std::string& to)
{
  std::ifstream in(from, std::ios::binary);
  std::ofstream out(to, std::ios::binary | std::ios::trunc);
  if (!in || !out)
    return false;
  out << in.rdbuf();
  return static_cast<bool>(out);
}

void MakeDirs(const std::string& path)
{
  for (size_t pos = 1; pos != std::string::npos;)
  {
    pos = path.find('/', pos + 1);
    mkdir(path.substr(0, pos).c_str(), 0755);
  }
}

} // namespace Paths

static std::string ConfigFile() { return Paths::ConfigDir() + "/config.ini"; }

// Índices = RETRO_DEVICE_ID_JOYPAD_*:
// 0 B(✕) 1 Y(□) 2 Select 3 Start 4 Arriba 5 Abajo 6 Izq. 7 Der. 8 A(◯) 9 X(△) 10 L1 11 R1 12 L2 13 R2 14 L3 15 R3
void Config::DefaultKeyboard()
{
  static const int defaults[kPsButtons] = {
    SDL_SCANCODE_Z,  SDL_SCANCODE_A,  SDL_SCANCODE_BACKSPACE, SDL_SCANCODE_RETURN,
    SDL_SCANCODE_UP, SDL_SCANCODE_DOWN, SDL_SCANCODE_LEFT,    SDL_SCANCODE_RIGHT,
    SDL_SCANCODE_X,  SDL_SCANCODE_S,  SDL_SCANCODE_Q,         SDL_SCANCODE_W,
    SDL_SCANCODE_E,  SDL_SCANCODE_R,  SDL_SCANCODE_C,         SDL_SCANCODE_V,
  };
  for (int i = 0; i < kPsButtons; i++)
    key_bind[i] = defaults[i];
}

void Config::DefaultPad()
{
  // Disposición posicional: el botón de abajo es ✕, como en un mando de PlayStation.
  static const int defaults[kPsButtons] = {
    SDL_CONTROLLER_BUTTON_A,
    SDL_CONTROLLER_BUTTON_X,
    SDL_CONTROLLER_BUTTON_BACK,
    SDL_CONTROLLER_BUTTON_START,
    SDL_CONTROLLER_BUTTON_DPAD_UP,
    SDL_CONTROLLER_BUTTON_DPAD_DOWN,
    SDL_CONTROLLER_BUTTON_DPAD_LEFT,
    SDL_CONTROLLER_BUTTON_DPAD_RIGHT,
    SDL_CONTROLLER_BUTTON_B,
    SDL_CONTROLLER_BUTTON_Y,
    SDL_CONTROLLER_BUTTON_LEFTSHOULDER,
    SDL_CONTROLLER_BUTTON_RIGHTSHOULDER,
    kPadAxisBase + SDL_CONTROLLER_AXIS_TRIGGERLEFT,
    kPadAxisBase + SDL_CONTROLLER_AXIS_TRIGGERRIGHT,
    SDL_CONTROLLER_BUTTON_LEFTSTICK,
    SDL_CONTROLLER_BUTTON_RIGHTSTICK,
  };
  for (int i = 0; i < kPsButtons; i++)
    pad_bind[i] = defaults[i];
}

void Config::Load()
{
  std::ifstream in(ConfigFile());
  std::string line;
  while (std::getline(in, line))
  {
    const size_t eq = line.find('=');
    if (eq == std::string::npos || line[0] == '#')
      continue;
    const std::string key = line.substr(0, eq);
    const std::string val = line.substr(eq + 1);
    const bool b = (val == "1" || val == "true");
    if (key == "game_path") game_path = val;
    else if (key == "bios_file") bios_file = val;
    else if (key == "hardware_renderer") hardware_renderer = b;
    else if (key == "resolution_scale") resolution_scale = std::atoi(val.c_str());
    else if (key == "pgxp") pgxp = b;
    else if (key == "texture_filter") texture_filter = val;
    else if (key == "side_art") side_art = b;
    else if (key == "widescreen") widescreen = b;
    else if (key == "smooth_filter") smooth_filter = b;
    else if (key == "fullscreen") fullscreen = b;
    else if (key == "volume") volume = std::atoi(val.c_str());
    else if (key == "music_volume") music_volume = std::atoi(val.c_str());
    else if (key == "sfx_volume") sfx_volume = std::atoi(val.c_str());
    else if (key == "fast_boot") fast_boot = b;
    else if (key == "prompt_style") prompt_style = val;
    else if (key == "cheats") cheats = val;
    else if (key == "hardcore") hardcore = b;
    else if (key.rfind("key_", 0) == 0 || key.rfind("pad_", 0) == 0)
    {
      const int idx = std::atoi(key.c_str() + 4);
      const int v = std::atoi(val.c_str());
      if (idx < 0 || idx >= kPsButtons)
        continue;
      if (key[0] == 'k' && v >= 0 && v < SDL_NUM_SCANCODES)
        key_bind[idx] = v;
      else if (key[0] == 'p' && v >= -1 &&
               (v < SDL_CONTROLLER_BUTTON_MAX || (v >= kPadAxisBase && v < kPadAxisBase + SDL_CONTROLLER_AXIS_MAX)))
        pad_bind[idx] = v;
    }
  }
  // 3x provoca texto ilegible y artefactos en Tenchu 2 (fallo del núcleo al copiar VRAM escalada).
  if (resolution_scale == 3) resolution_scale = 4;
  if (resolution_scale < 1) resolution_scale = 1;
  if (resolution_scale > 5) resolution_scale = 5;
  if (volume < 0) volume = 0;
  if (volume > 100) volume = 100;
}

void Config::Save() const
{
  Paths::MakeDirs(Paths::ConfigDir());
  std::ofstream out(ConfigFile());
  out << "# Tenshu 2 - configuración del launcher\n";
  out << "game_path=" << game_path << "\n";
  out << "bios_file=" << bios_file << "\n";
  out << "hardware_renderer=" << hardware_renderer << "\n";
  out << "resolution_scale=" << resolution_scale << "\n";
  out << "pgxp=" << pgxp << "\n";
  out << "texture_filter=" << texture_filter << "\n";
  out << "side_art=" << side_art << "\n";
  out << "widescreen=" << widescreen << "\n";
  out << "smooth_filter=" << smooth_filter << "\n";
  out << "fullscreen=" << fullscreen << "\n";
  out << "volume=" << volume << "\n";
  out << "music_volume=" << music_volume << "\n";
  out << "sfx_volume=" << sfx_volume << "\n";
  out << "fast_boot=" << fast_boot << "\n";
  out << "prompt_style=" << prompt_style << "\n";
  out << "cheats=" << cheats << "\n";
  out << "hardcore=" << hardcore << "\n";
  for (int i = 0; i < kPsButtons; i++)
    out << "key_" << i << "=" << key_bind[i] << "\n";
  for (int i = 0; i < kPsButtons; i++)
    out << "pad_" << i << "=" << pad_bind[i] << "\n";
}
