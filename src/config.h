#pragma once
#include <string>
#include <vector>

// Botones del mando de PlayStation; el índice es RETRO_DEVICE_ID_JOYPAD_* (0..15).
constexpr int kPsButtons = 16;
// Asignación del mando: 0..99 = botón SDL_GameControllerButton, 100+eje = gatillo, -1 = sin asignar.
constexpr int kPadAxisBase = 100;
// Botones del ratón en las asignaciones de teclado (esquema teclado y ratón).
constexpr int kMouseLeft = 1001, kMouseRight = 1002, kMouseMiddle = 1003, kWheelUp = 1004, kWheelDown = 1005;
// Esquema fijo de teclado y ratón: WASD, ratón para la cámara, Espacio salta, Ctrl agacha, clic
// izquierdo ataca, clic derecho usa la herramienta, la rueda cambia de herramienta…
int KbMouseBind(int retro_id);

struct Config
{
  Config() { DefaultKeyboard(); DefaultPad(); }
  void DefaultKeyboard();
  void DefaultPad();

  int key_bind[kPsButtons]; // SDL_Scancode por botón (0 = sin asignar)
  bool kb_mouse = false;    // esquema de teclado y ratón en lugar de key_bind
  // Tecla (o botón del ratón) que corresponde a un botón de PS1 con el esquema elegido.
  int KeyBind(int retro_id) const { return kb_mouse ? KbMouseBind(retro_id) : key_bind[retro_id]; }
  int pad_bind[kPsButtons]; // ver kPadAxisBase

  std::string game_path;        // .cue / .bin / .chd seleccionado por el usuario
  std::string bios_file;        // BIOS elegida dentro de bios/ (vacío = OpenBIOS integrada)

  bool hardware_renderer = true; // OpenGL (permite escalado) o software (máxima precisión)
  int resolution_scale = 4;      // 1, 2, 4 o 5 (3x corrompe el texto de Tenchu 2 en SwanStation)
  std::string texture_filter = "xBR"; // Nearest, Bilinear, JINC2, xBR (suavizado "HD" de texturas)
  bool pgxp = true;              // corrige el "temblor" de polígonos
  bool pgxp_cpu = true;          // PGXP también para la geometría que el juego calcula con la CPU
  bool widescreen = false;       // 16:9 mediante parche del propio juego (si existe para el disco)
  bool side_art = true;          // rellenar los laterales (4:3 en pantalla ancha) con la ilustración
  bool smooth_filter = false;    // filtrado bilineal al escalar la imagen
  bool fullscreen = false;
  int volume = 100;              // 0..100, sonido del juego
  int music_volume = 70;         // 0..100, música del launcher
  int sfx_volume = 80;           // 0..100, efectos del launcher
  bool fast_boot = true;         // salta la intro de la BIOS
  bool hardcore = false;         // modo hardcore: BIOS original, sin estados guardados ni trucos
  std::string cheats;            // nombres de los trucos activos, separados por '|'
  bool game_prompts = true;
  bool free_camera = true;       // cámara libre con el joystick derecho (no en hardcore)
  int camera_speed = 100;
  bool modern_controls = true;   // movimiento 360° relativo a la cámara (si no, el de tanque original)        // velocidad de la cámara libre, 20..300 %      // cambiar también los iconos de botones dentro del juego
  std::string prompt_style = "auto"; // iconos de botones: auto, keyboard, xbox, playstation
  std::string language;              // "es" o "en"; vacío = según el idioma del sistema

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
std::string UserDir();      // junto al ejecutable, o junto al .AppImage si se ejecuta así
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
