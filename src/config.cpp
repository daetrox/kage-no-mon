#include "config.h"

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
std::string BiosDir() { return ExeDir() + "/bios"; }
std::string GameDir() { return ExeDir() + "/game"; }

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
}
