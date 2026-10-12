#pragma once
#include <string>
#include <vector>

struct Config
{
  std::string game_path;        // .cue / .bin / .chd seleccionado por el usuario
  std::string bios_file;        // BIOS elegida dentro de bios/ (vacío = automática según la región)

  bool hardware_renderer = true; // OpenGL (permite escalado) o software (máxima precisión)
  int resolution_scale = 4;      // 1, 2, 4 o 5 (3x corrompe el texto de Tenchu 2 en SwanStation)
  std::string texture_filter = "xBR"; // Nearest, Bilinear, JINC2, xBR (suavizado "HD" de texturas)
  bool pgxp = true;              // corrige el "temblor" de polígonos
  bool widescreen = false;       // 16:9 mediante parche del propio juego (si existe para el disco)
  bool side_art = true;          // rellenar los laterales (4:3 en pantalla ancha) con la ilustración
  bool smooth_filter = false;    // filtrado bilineal al escalar la imagen
  bool fullscreen = false;
  int volume = 100;              // 0..100, sonido del juego
  int music_volume = 70;         // 0..100, música del launcher
  int sfx_volume = 80;           // 0..100, efectos del launcher
  bool fast_boot = true;         // salta la intro de la BIOS
  std::string prompt_style = "auto"; // iconos de botones: auto, keyboard, xbox, playstation

  void Load();
  void Save() const;
};

namespace Paths {
std::string ExeDir();       // carpeta del ejecutable
std::string AssetsDir();    // <exe>/assets
std::string CorePath();     // <exe>/core/swanstation_libretro.so
std::string ConfigDir();    // ~/.config/tenshu2
std::string DataDir();      // ~/.local/share/tenshu2
std::string SavesDir();     // tarjetas de memoria
std::string StatesDir();    // estados guardados
std::string BiosDir();      // <exe>/bios: BIOS originales del usuario (no se distribuyen)
std::string GameDir();      // <exe>/game: imagen del juego del usuario (no se distribuye)
// Busca BIOS (archivos de 512 KB) en BiosDir(). Devuelve sus nombres.
std::vector<std::string> FindBiosFiles();
bool FileExists(const std::string& path);
// Número de serie del disco (p. ej. "SLES-02465") leído de SYSTEM.CNF; vacío si no se puede leer.
std::string ReadDiscSerial(const std::string& image_path);
bool CopyFile(const std::string& from, const std::string& to);
void MakeDirs(const std::string& path);
} // namespace Paths
