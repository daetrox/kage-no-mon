// Tenshu 2 - Launcher
// Lanzador independiente que integra el núcleo SwanStation (libretro) para que el juego se
// sienta como una aplicación nativa: sin menús ni mensajes del emulador.
//
// La interfaz se dibuja sobre la ilustración del launcher (assets/ui/). Todas las posiciones se
// expresan en píxeles de esa imagen (1536x1024) y se escalan a la altura de la ventana.
#include "achievements.h"
#include "audio.h"
#include "cheats.h"
#include "free_camera.h"
#include "game_buttons.h"
#include "platform.h"
#include "config.h"
#include "emulator.h"
#include "gl.h"
#include "libretro.h"
#include "renderer.h"

extern "C" {
#include "md5.h"
}

#include <SDL.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <functional>
#include <map>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace {

constexpr const char* kVersion = "v1.0.1";
constexpr float kImgW = 1536.0f;
constexpr float kImgH = 1024.0f;
constexpr float kTitleH = 36.0f;

const Color kWhite = {0.95f, 0.93f, 0.9f, 1.0f};
const Color kGrey = {0.74f, 0.72f, 0.7f, 1.0f};
const Color kDim = {0.55f, 0.53f, 0.52f, 1.0f};
const Color kRed = {0.86f, 0.1f, 0.07f, 1.0f};
const Color kGold = {1.0f, 0.78f, 0.3f, 1.0f};

Color WithAlpha(Color c, float a) { return {c.r, c.g, c.b, c.a * a}; }

float Clamp01(float x) { return std::clamp(x, 0.0f, 1.0f); }
float EaseOut(float x)
{
  x = Clamp01(x);
  return 1.0f - (1.0f - x) * (1.0f - x) * (1.0f - x);
}
float EaseInOut(float x)
{
  x = Clamp01(x);
  return x < 0.5f ? 4 * x * x * x : 1.0f - std::pow(-2.0f * x + 2.0f, 3.0f) / 2.0f;
}
float Smooth(float a, float b, float x)
{
  const float t = Clamp01((x - a) / (b - a));
  return t * t * (3 - 2 * t);
}

// Duraciones de las animaciones de apertura y cierre (segundos).
constexpr float kIntroLen = 2.5f;
constexpr float kOutroLen = 1.25f;

std::string BaseName(const std::string& path)
{
  const size_t s = path.find_last_of("/\\");
  return s == std::string::npos ? path : path.substr(s + 1);
}

std::string StemName(const std::string& path)
{
  std::string b = BaseName(path);
  const size_t d = b.find_last_of('.');
  return d == std::string::npos ? b : b.substr(0, d);
}

std::string Lower(std::string s)
{
  for (char& c : s)
    c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
  return s;
}

bool HasExt(const std::string& path, const char* ext)
{
  const std::string l = Lower(path);
  const size_t n = std::strlen(ext);
  return l.size() > n && l.compare(l.size() - n, n, ext) == 0;
}

bool IsDiscImage(const std::string& path)
{
  for (const char* ext : {".cue", ".chd", ".bin", ".iso", ".img", ".pbp", ".ecm", ".m3u"})
    if (HasExt(path, ext))
      return true;
  return false;
}

// Quita el último carácter UTF-8 completo.
void PopUtf8(std::string& s)
{
  while (!s.empty() && (static_cast<unsigned char>(s.back()) & 0xC0) == 0x80)
    s.pop_back();
  if (!s.empty())
    s.pop_back();
}

// Busca la imagen del disco: primero en <exe>/game, después junto al ejecutable y carpetas superiores.
std::string AutoDetectGame()
{
  std::vector<std::string> dirs = {Paths::GameDir(), Paths::UserDir()};
  std::string d = Paths::ExeDir();
  for (int i = 0; i < 4; i++)
  {
    dirs.push_back(d);
    const size_t s = d.find_last_of("/\\");
    if (s == std::string::npos || s == 0)
      break;
    d = d.substr(0, s);
  }
  for (const std::string& dir : dirs)
  {
    std::string best;
    std::error_code ec;
    for (const std::filesystem::directory_entry& e : std::filesystem::directory_iterator(std::filesystem::u8path(dir), ec))
    {
      const std::string name = e.path().filename().u8string();
      if (!IsDiscImage(name))
        continue;
      const bool pref = HasExt(name, ".cue") || HasExt(name, ".chd");
      if (best.empty() || pref)
        best = dir + "/" + name;
    }
    if (!best.empty())
      return best;
  }
  return {};
}

std::vector<std::string> SplitLines(const std::string& s)
{
  std::vector<std::string> out;
  std::stringstream ss(s);
  std::string line;
  while (std::getline(ss, line))
    out.push_back(line);
  return out;
}

// --- Selector de archivos nativo (Windows: diálogo del sistema; Linux: zenity / kdialog), en un
// hilo aparte ---
class FilePicker
{
public:
  static bool Available()
  {
#ifdef _WIN32
    return true;
#else
    return std::system("command -v zenity >/dev/null 2>&1") == 0 ||
           std::system("command -v kdialog >/dev/null 2>&1") == 0;
#endif
  }

  void Open(const std::string& title, const std::string& patterns, std::function<void(const std::string&)> cb)
  {
    if (m_busy)
      return;
    m_busy = true;
    m_done = false;
    m_callback = std::move(cb);
    std::thread([this, title, patterns]() {
#ifdef _WIN32
      const std::string result = Platform::OpenFileDialog(title, patterns);
#else
      std::string cmd;
      if (std::system("command -v zenity >/dev/null 2>&1") == 0)
        cmd = "zenity --file-selection --title=\"" + title + "\" --file-filter=\"" + title + " | " + patterns +
              "\" --file-filter=\"Todos | *\" 2>/dev/null";
      else
        cmd = "kdialog --getopenfilename \"$HOME\" \"" + patterns + "\" --title \"" + title + "\" 2>/dev/null";
      std::string result;
      if (FILE* f = popen(cmd.c_str(), "r"))
      {
        char buf[4096];
        while (fgets(buf, sizeof(buf), f))
          result += buf;
        pclose(f);
      }
      while (!result.empty() && (result.back() == '\n' || result.back() == '\r'))
        result.pop_back();
#endif
      std::lock_guard<std::mutex> lock(m_mutex);
      m_result = result;
      m_done = true;
    }).detach();
  }

  void Poll()
  {
    if (!m_busy || !m_done)
      return;
    std::string r;
    {
      std::lock_guard<std::mutex> lock(m_mutex);
      r = m_result;
    }
    m_busy = false;
    if (!r.empty() && m_callback)
      m_callback(r);
  }

private:
  std::atomic<bool> m_busy{false};
  std::atomic<bool> m_done{false};
  std::mutex m_mutex;
  std::string m_result;
  std::function<void(const std::string&)> m_callback;
};

enum class Screen
{
  Menu,
  Options,
  Controls,
  Remap,
  Credits,
  Cheats,
  Achievements,
  Game,
};

enum class Modal
{
  None,
  Dialog,
  PlayChoice,
  ConfirmReset, // confirmar el reinicio de los logros
};

enum class PromptStyle
{
  Keyboard,
  Xbox,
  PlayStation,
};

// Botones del mando de PlayStation (lo que espera el juego) y acciones de interfaz.
enum class PsButton
{
  Cross,
  Circle,
  Square,
  Triangle,
  L1,
  R1,
  L2,
  R2,
  L3,
  R3,
  Start,
  Select,
  DPad,
  LStick,
  RStick,
  DUp,    // direcciones sueltas de la cruceta (pantalla de personalización)
  DDown,
  DLeft,
  DRight,
  Accept, // aceptar en menús
  Back,   // volver en menús
  Pause,  // abrir la pausa durante el juego
};

struct Rect
{
  float x = 0, y = 0, w = 0, h = 0;
  bool Contains(float px, float py) const { return px >= x && px <= x + w && py >= y && py <= y + h; }
};

// Zona interactiva registrada al dibujar; los eventos de ratón usan las del último frame.
struct HotZone
{
  Rect r;
  int group; // 0 menú, 1 opciones, 2 pausa, 3 elección, 4 login, 5 barra de título, 6 genérico
  int index;
  std::function<void(float, float)> click;
};

struct Popup
{
  uint32_t id;
  float time;
};

// Botones del menú (JUGAR, LOGROS, OPCIONES, CRÉDITOS, SALIR), en píxeles de la ilustración.
constexpr int kMenuItems = 5;
const Rect kMenuSlots[kMenuItems] = {
  {66, 388, 462, 86},
  {72, 478, 428, 82},
  {72, 566, 424, 82},
  {72, 654, 424, 82},
  {72, 742, 424, 82},
};

enum OptRow
{
  OPT_GAME,
  OPT_BIOS,
  OPT_BIOS_LOAD,
  OPT_FASTBOOT,
  OPT_HARDCORE,
  OPT_RENDERER,
  OPT_SCALE,
  OPT_TEXFILTER,
  OPT_PGXP,
  OPT_WIDESCREEN,
  OPT_SIDEART,
  OPT_SMOOTH,
  OPT_FULLSCREEN,
  OPT_VOLUME,
  OPT_MUSIC,
  OPT_SFX,
  OPT_PROMPTS,
  OPT_GAME_PROMPTS,
  OPT_FREE_CAMERA,
  OPT_CAMERA_SPEED,
  OPT_MOVEMENT,
  OPT_KB_SCHEME,
  OPT_REMAP_KB,
  OPT_REMAP_PAD,
  OPT_RESET_CONTROLS,
  OPT_CONTROLS,
  OPT_BACK,
};

constexpr int kPauseItems = 6;

// Opciones: lista de categorías (página -1) y sus páginas (0..3).
constexpr int kOptCategoryCount = 5;
constexpr int kCheatsCategory = 4; // abre la pantalla de trucos en lugar de una página de filas
const char* const kOptCategories[kOptCategoryCount] = {"ISO Y BIOS", "GRÁFICOS", "SONIDO", "CONTROLES", "TRUCOS"};
const char* const kOptKanji[kOptCategoryCount] = {"盤", "絵", "音", "操", "術"};
const char* const kOptCategoryHelp[kOptCategoryCount] = {
  "Imagen del disco, BIOS de la consola, arranque y modo hardcore.",
  "Resolución, filtros, aspecto de la pantalla y pantalla completa.",
  "Volumen del juego, de la música y de los efectos.",
  "Iconos de botones, personalización y esquema de controles.",
  "Vida infinita, misiones desbloqueadas y más. Mientras haya trucos activos, los logros se pausan.",
};

const std::vector<int>& OptPageRows(int page)
{
  static const std::vector<int> pages[kOptCategoryCount] = {
    {OPT_GAME, OPT_BIOS, OPT_BIOS_LOAD, OPT_FASTBOOT, OPT_HARDCORE, OPT_BACK},
    {OPT_RENDERER, OPT_SCALE, OPT_TEXFILTER, OPT_PGXP, OPT_WIDESCREEN, OPT_SIDEART, OPT_SMOOTH, OPT_FULLSCREEN,
     OPT_BACK},
    {OPT_VOLUME, OPT_MUSIC, OPT_SFX, OPT_BACK},
    {OPT_PROMPTS, OPT_GAME_PROMPTS, OPT_KB_SCHEME, OPT_MOVEMENT, OPT_FREE_CAMERA, OPT_CAMERA_SPEED, OPT_REMAP_KB, OPT_REMAP_PAD, OPT_RESET_CONTROLS, OPT_CONTROLS, OPT_BACK},
    {OPT_BACK}, // TRUCOS tiene su propia pantalla
  };
  return pages[std::clamp(page, 0, kOptCategoryCount - 1)];
}

// Nombre legible de una BIOS de bios/: modelo y región si se reconoce su huella MD5.
std::string BiosLabel(const std::string& file)
{
  static std::map<std::string, std::string> cache;
  auto it = cache.find(file);
  if (it != cache.end())
    return it->second;
  static const std::pair<const char*, const char*> known[] = {
    {"239665b1a3dade1b5a52c06338011044", "SCPH-1000 · Japón"},
    {"924e392ed05558ffdb115408c263dccf", "SCPH-1001 · EE. UU."},
    {"54847e693405ffeb0359c6287434cbef", "SCPH-1002 · Europa"},
    {"8dd7d5296a650fac7319bce665a6a53c", "SCPH-5500 · Japón"},
    {"490f666e1afb15b7362b406ed1cea246", "SCPH-5501 · EE. UU."},
    {"32736f17079d0b2b7024407c39bd3050", "SCPH-5502 · Europa"},
    {"1e68c231d0896b7eadcad1d7d8e76129", "SCPH-7001 · EE. UU."},
    {"b9d9a0286c33dc6b7237bb13cd46fdee", "SCPH-7502 · Europa"},
    {"6e3735ff4c7dc899ee98981385f6f3d0", "SCPH-101 · EE. UU."},
  };
  std::string label = file;
  if (FILE* f = Platform::OpenFile((Paths::BiosDir() + "/" + file).c_str(), "rb"))
  {
    std::vector<unsigned char> data(512 * 1024);
    const size_t n = std::fread(data.data(), 1, data.size(), f);
    std::fclose(f);
    md5_state_t st;
    md5_byte_t digest[16];
    md5_init(&st);
    md5_append(&st, data.data(), static_cast<int>(n));
    md5_finish(&st, digest);
    char hex[33];
    for (int i = 0; i < 16; i++)
      std::snprintf(hex + i * 2, 3, "%02x", digest[i]);
    for (const auto& [h, name] : known)
      if (std::strcmp(h, hex) == 0)
        label = std::string(name) + "  (" + file + ")";
  }
  cache[file] = label;
  return label;
}

// --- Asignación de controles ---

// Orden de los botones en la pantalla de personalización (índices RETRO_DEVICE_ID_JOYPAD_*).
const int kRemapOrder[kPsButtons] = {
  RETRO_DEVICE_ID_JOYPAD_UP, RETRO_DEVICE_ID_JOYPAD_DOWN, RETRO_DEVICE_ID_JOYPAD_LEFT, RETRO_DEVICE_ID_JOYPAD_RIGHT,
  RETRO_DEVICE_ID_JOYPAD_B,  RETRO_DEVICE_ID_JOYPAD_A,    RETRO_DEVICE_ID_JOYPAD_Y,    RETRO_DEVICE_ID_JOYPAD_X,
  RETRO_DEVICE_ID_JOYPAD_L,  RETRO_DEVICE_ID_JOYPAD_R,    RETRO_DEVICE_ID_JOYPAD_L2,   RETRO_DEVICE_ID_JOYPAD_R2,
  RETRO_DEVICE_ID_JOYPAD_L3, RETRO_DEVICE_ID_JOYPAD_R3,   RETRO_DEVICE_ID_JOYPAD_START, RETRO_DEVICE_ID_JOYPAD_SELECT,
};

const char* PsButtonName(int retro_id)
{
  switch (retro_id)
  {
    case RETRO_DEVICE_ID_JOYPAD_UP: return "Arriba";
    case RETRO_DEVICE_ID_JOYPAD_DOWN: return "Abajo";
    case RETRO_DEVICE_ID_JOYPAD_LEFT: return "Izquierda";
    case RETRO_DEVICE_ID_JOYPAD_RIGHT: return "Derecha";
    case RETRO_DEVICE_ID_JOYPAD_B: return "Cruz";
    case RETRO_DEVICE_ID_JOYPAD_A: return "Círculo";
    case RETRO_DEVICE_ID_JOYPAD_Y: return "Cuadrado";
    case RETRO_DEVICE_ID_JOYPAD_X: return "Triángulo";
    case RETRO_DEVICE_ID_JOYPAD_L: return "L1";
    case RETRO_DEVICE_ID_JOYPAD_R: return "R1";
    case RETRO_DEVICE_ID_JOYPAD_L2: return "L2";
    case RETRO_DEVICE_ID_JOYPAD_R2: return "R2";
    case RETRO_DEVICE_ID_JOYPAD_L3: return "L3";
    case RETRO_DEVICE_ID_JOYPAD_R3: return "R3";
    case RETRO_DEVICE_ID_JOYPAD_START: return "Start";
    case RETRO_DEVICE_ID_JOYPAD_SELECT: return "Select";
  }
  return "?";
}

// Teclas que el usuario no puede asignar: Esc abre la pausa y las F están ocupadas por el launcher.
bool IsReservedKey(SDL_Scancode sc)
{
  return sc == SDL_SCANCODE_ESCAPE || (sc >= SDL_SCANCODE_F1 && sc <= SDL_SCANCODE_F12);
}

std::string KeyLabel(int scancode)
{
  if (scancode <= 0)
    return "—";
  switch (scancode)
  {
    case kMouseLeft: return "Clic izq.";
    case kMouseRight: return "Clic der.";
    case kMouseMiddle: return "Clic central";
    case kWheelUp: return "Rueda arriba";
    case kWheelDown: return "Rueda abajo";
    default: break;
  }
  const auto sc = static_cast<SDL_Scancode>(scancode);
  switch (sc)
  {
    case SDL_SCANCODE_RETURN: return "Enter";
    case SDL_SCANCODE_KP_ENTER: return "Enter (num.)";
    case SDL_SCANCODE_BACKSPACE: return "Retroceso";
    case SDL_SCANCODE_SPACE: return "Espacio";
    case SDL_SCANCODE_TAB: return "Tab";
    case SDL_SCANCODE_UP: return "Arriba";
    case SDL_SCANCODE_DOWN: return "Abajo";
    case SDL_SCANCODE_LEFT: return "Izquierda";
    case SDL_SCANCODE_RIGHT: return "Derecha";
    case SDL_SCANCODE_LSHIFT: return "Mayús izq.";
    case SDL_SCANCODE_RSHIFT: return "Mayús der.";
    case SDL_SCANCODE_LCTRL: return "Ctrl izq.";
    case SDL_SCANCODE_RCTRL: return "Ctrl der.";
    case SDL_SCANCODE_LALT: return "Alt";
    case SDL_SCANCODE_RALT: return "Alt Gr";
    case SDL_SCANCODE_CAPSLOCK: return "Bloq Mayús";
    default: break;
  }
  const char* name = SDL_GetScancodeName(sc);
  return (name && *name) ? name : "?";
}

} // namespace

class App
{
public:
  int Run(int argc, char** argv);

private:
  bool Init(std::string* error);
  void Shutdown();
  void HandleEvent(const SDL_Event& e);
  void Update(float dt);
  void Render();

  // Dibujo
  void Layout();
  void RenderTitleBar();
  void RenderMenu();
  void RenderOptions();
  void RenderOptionCategories();
  void RenderRemap();
  void RenderCredits();
  void RenderCheats();
  bool CheatEnabled(const std::string& name) const;
  void SetCheatEnabled(const std::string& name, bool on);
  std::vector<std::string> ActiveCheatCodes() const;
  void RenderConfirmReset();
  void AssignBinding(int retro_id, int value);
  void RenderControls();
  void RenderAchievements();
  void RenderGame();
  void RenderPause();
  void RenderPlayChoice();
  void RenderDialog();
  void RenderToast();
  void RenderPopups();
  void RenderTransition();
  void RenderHints(const std::vector<std::pair<PsButton, std::string>>& hints, float right_x, float y);
  float DrawGlyph(PsButton b, float x, float y, float h, PromptStyle style, bool draw = true);
  float DrawGlyph(PsButton b, float x, float y, float h) { return DrawGlyph(b, x, y, h, CurrentStyle()); }
  void DrawMenuButton(const Rect& slot, const std::string& label, const Texture* icon, float sel, float font,
                      bool own_background, float alpha = 1.0f, float dx = 0.0f);
  void Panel(float x, float y, float w, float h);
  void DimScreen(float a);
  std::vector<std::string> Wrap(const Font& f, const std::string& text, float size, float max_w);
  std::string Truncate(const Font& f, const std::string& text, float size, float max_w);
  // Texto limitado a `max_w`: si no cabe se recorta con "…", o se desplaza si `scroll` es true.
  void FitText(const Font& f, const std::string& text, float x, float y, float max_w, float size, Color c,
               Align align, bool scroll, int key);

  // Acciones
  void ActivateMenu(int index);
  void ActivatePause(int index);
  void StartGame(bool resume);
  void StopGame(bool autosave);
  void OptionChange(int row, int dir);
  void OpenOptionPage(int page);
  void CycleBios(int dir);
  void ShowDialog(const std::string& text);
  void Toast(const std::string& text)
  {
    m_toast = text;
    m_toast_time = 2.5f;
  }
  void RequestQuit();
  void ToggleFullscreen();
  void ImportBios(const std::string& path);
  void SetGamePath(const std::string& path);
  std::string StatePath() const
  {
    if (const char* test = std::getenv("TENSHU_TEST_STATE")) // pruebas: nunca el estado del usuario
      return test;
    return Paths::StatesDir() + "/" + StemName(m_cfg.game_path) + ".state";
  }
  bool HasResumeState() const { return Paths::FileExists(StatePath()); }
  // Controles 360° (necesitan la cámara libre: con la cámara del juego detrás del personaje, correr
  // hacia un lado haría girar a ambos en círculo).
  bool ModernControls() const { return m_cfg.modern_controls && m_freecam.Available(); }
  // Guarda el estado con la tabla de cámara original (ver FreeCamera::Restore).
  bool SaveStateNow()
  {
    size_t n = 0;
    if (const uint8_t* ram = m_emu.SystemRam(&n))
      m_freecam.Restore(const_cast<uint8_t*>(ram));
    return m_emu.SaveState(StatePath());
  }
  bool HasSonyBios() const
  {
    return !m_cfg.bios_file.empty() && Paths::FileExists(Paths::BiosDir() + "/" + m_cfg.bios_file);
  }
  // En hardcore no se pueden usar estados guardados (norma de RetroAchievements).
  bool StatesBlocked()
  {
    if (!m_hardcore_session)
      return false;
    m_audio.Play(Sfx::Cancel);
    Toast("No disponible en modo hardcore");
    return true;
  }
  void StartLogin();
  bool IntroActive() const { return m_intro_t < kIntroLen; }

  // Entrada
  PromptStyle CurrentStyle() const;
  PromptStyle PadStyle() const;
  // Iconos de botones dentro del juego para el mando en uso.
  std::vector<uint8_t> GameButtonPatches() const
  {
    Config cfg = m_cfg;
    for (int id = 0; id < kPsButtons; id++)
      cfg.key_bind[id] = m_cfg.KeyBind(id);
    GameButtonStyle style = GameButtonStyle::Original;
    if (m_cfg.game_prompts && CurrentStyle() == PromptStyle::Keyboard)
      style = GameButtonStyle::Keyboard;
    else if (m_cfg.game_prompts && CurrentStyle() == PromptStyle::Xbox)
      style = GameButtonStyle::Xbox;
    return BuildGameButtonPatches(style, cfg);
  }
  void Navigate(int dy, int dx);
  void NavigateImpl(int dy, int dx);
  void Accept();
  void Back();
  EmuInput GatherGameInput();
  void OpenController(int index);
  bool TextFocus() const
  {
    return m_screen == Screen::Achievements && m_modal == Modal::None && m_login_focus < 2 &&
           m_ach.GetState() == Achievements::State::NeedLogin;
  }

  // Coordenadas de imagen -> pantalla
  float IX(float x) const { return m_ox + x * m_s; }
  float IY(float y) const { return m_oy + y * m_s; }
  float IS(float v) const { return v * m_s; }
  Rect IR(const Rect& r) const { return {IX(r.x), IY(r.y), IS(r.w), IS(r.h)}; }
  void Hot(const Rect& screen_rect, int group, int index, std::function<void(float, float)> click)
  {
    m_hot.push_back({screen_rect, group, index, std::move(click)});
  }

  static SDL_HitTestResult HitTest(SDL_Window* win, const SDL_Point* pt, void* data);

  SDL_Window* m_window = nullptr;
  SDL_GLContext m_gl = nullptr;
  Renderer m_r;
  Emulator m_emu;
  Config m_cfg;
  FilePicker m_picker;
  Achievements m_ach;
  Audio m_audio;

  Texture m_bg, m_fire, m_btn_sel, m_btn_normal;
  Texture m_ic_play, m_ic_ach, m_ic_opt, m_ic_quit;
  Texture m_logo, m_tagline;

  Screen m_screen = Screen::Menu;
  Modal m_modal = Modal::None;
  bool m_running = true;
  bool m_paused = false;
  int m_sel = 0;
  int m_opt_page = -1; // -1 = categorías
  int m_opt_sel = 0;
  int m_pause_sel = 0;
  int m_choice_sel = 0;
  int m_login_focus = 0; // 0 usuario, 1 contraseña, 2 botón
  float m_ach_scroll = 0, m_ach_scroll_target = 0;
  int m_ach_sel = 0;
  std::string m_login_user, m_login_pass;
  float m_sel_anim[8] = {};
  float m_time = 0;
  float m_fade = 0;
  float m_intro_t = 0; // tiempo desde que se abrió el launcher
  bool m_intro_sound = false;
  float m_outro_t = -1; // >= 0 mientras se reproduce la animación de cierre
  int m_w = 1200, m_h = 800;
  float m_s = 1, m_ox = 0, m_oy = 0;
  Rect m_bg_rect; // dónde se dibuja la ilustración completa (puede salirse de la pantalla)
  float m_mouse_px = -1, m_mouse_py = -1;
  float m_mouse_idle = 0;
  std::string m_dialog;
  std::string m_toast;
  float m_toast_time = 0;
  std::vector<HotZone> m_hot;
  std::vector<Popup> m_popups;
  std::string m_ach_game_path;
  std::string m_ws_serial_path;
  bool m_ws_available = false;
  std::vector<Cheat> m_cheat_list; // trucos del disco actual (pantalla TRUCOS)
  int m_cheat_sel = 0;
  float m_cheat_scroll = 0;
  bool m_cheats_active = false;    // la partida en curso usa trucos: logros en pausa
  bool m_hardcore_session = false; // la partida en curso es hardcore: sin estados guardados
  bool m_test_saved = false;
  FreeCamera m_freecam;
  EmuInput m_last_input;       // lo último enviado al juego (para la cámara libre)
  bool m_r3_was_down = false;
  bool m_l3_was_down = false;
  bool m_mouse_captured = false;       // ratón capturado para la cámara (esquema teclado y ratón)
  float m_look_dx = 0, m_look_dy = 0;  // movimiento del ratón pendiente
  float m_wheel_up_t = 0, m_wheel_down_t = 0; // pulsación breve de L2/R2 con la rueda
  bool m_mark_visible = false; // marca del enemigo fijado
  float m_mark_x = 0, m_mark_y = 0, m_mark_alpha = 0;
  float m_test_rstick[2] = {}; // pruebas: joystick derecho simulado
  float m_test_lstick[2] = {}; // y el izquierdo
  float m_move[2] = {};        // dirección pedida para los controles 360° (relativa a la cámara)
  float m_credits_scroll = 0, m_credits_target = 0;
  float m_credits_height = 0; // alto total del texto de créditos (px de la ilustración)
  int m_remap_device = 0;   // 0 teclado, 1 mando
  int m_remap_sel = 0;      // 0..15 botones, 16 restaurar, 17 volver
  int m_remap_wait = -1;    // botón (RETRO_DEVICE_ID_JOYPAD_*) que espera una tecla/botón nuevo
  float m_remap_timer = 0;
  float m_reset_confirm = 0; // > 0: "Restaurar controles" pide una segunda pulsación
  int m_marquee_key = -1; // fila cuyo texto se está desplazando
  float m_marquee_start = 0;

  Rect m_tb_min, m_tb_max, m_tb_close;

  SDL_GameController* m_pad = nullptr;
  PromptStyle m_last_device = PromptStyle::Keyboard;
  float m_axis_repeat = 0;

  // Pruebas automáticas (--screenshot).
  uint16_t m_test_buttons = 0;
  std::string m_screenshot_path;
  float m_screenshot_time = 2.0f;
};

// ------------------------------------------------------------------------------------------------
// Inicialización
// ------------------------------------------------------------------------------------------------

bool App::Init(std::string* error)
{
  SDL_SetHint("SDL_VIDEO_WAYLAND_WMCLASS", "tenshu2");
  SDL_SetHint("SDL_VIDEO_X11_WMCLASS", "tenshu2");
  SDL_SetHint(SDL_HINT_APP_NAME, "Tenshu 2");
  if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMECONTROLLER | SDL_INIT_TIMER) != 0)
  {
    *error = SDL_GetError();
    return false;
  }

  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
  SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
  SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
  SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);

  Uint32 flags = SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_BORDERLESS | SDL_WINDOW_ALLOW_HIGHDPI;
  if (m_cfg.fullscreen)
    flags |= SDL_WINDOW_FULLSCREEN_DESKTOP;
  // 3:2, la proporción de la ilustración.
  m_window = SDL_CreateWindow("Tenshu 2", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 1200, 800, flags);
  if (!m_window)
  {
    *error = SDL_GetError();
    return false;
  }
  SDL_SetWindowMinimumSize(m_window, 900, 600);

  m_gl = SDL_GL_CreateContext(m_window);
  if (!m_gl)
  {
    *error = std::string("OpenGL 3.3 no disponible: ") + SDL_GetError();
    return false;
  }
  SDL_GL_MakeCurrent(m_window, m_gl);
  if (!LoadGLFunctions())
  {
    *error = "Faltan funciones de OpenGL 3.3";
    return false;
  }
  SDL_GL_SetSwapInterval(1);
  SDL_SetWindowHitTest(m_window, &App::HitTest, this);

  if (!m_r.Init(Paths::AssetsDir(), error))
    return false;

  const std::string ui = Paths::AssetsDir() + "/ui/";
  bool ok = m_r.LoadTexture(ui + "background_clean.jpg", &m_bg, true);
  ok &= m_r.LoadTexture(ui + "logo.png", &m_logo, true);
  ok &= m_r.LoadTexture(ui + "tagline.png", &m_tagline, true);
  ok &= m_r.LoadTexture(ui + "fire_mask.png", &m_fire, true);
  ok &= m_r.LoadTexture(ui + "btn_selected.png", &m_btn_sel, false);
  ok &= m_r.LoadTexture(ui + "btn_normal.png", &m_btn_normal, false);
  ok &= m_r.LoadTexture(ui + "icon_play.png", &m_ic_play, true);
  ok &= m_r.LoadTexture(ui + "icon_achievements.png", &m_ic_ach, true);
  ok &= m_r.LoadTexture(ui + "icon_options.png", &m_ic_opt, true);
  ok &= m_r.LoadTexture(ui + "icon_quit.png", &m_ic_quit, true);
  if (!ok)
  {
    *error = "Faltan imágenes de la interfaz en " + ui;
    return false;
  }

  // Icono de la ventana: shuriken claro sobre disco rojo.
  if (SDL_Surface* icon = SDL_CreateRGBSurfaceWithFormat(0, 64, 64, 32, SDL_PIXELFORMAT_RGBA32))
  {
    auto* px = static_cast<Uint32*>(icon->pixels);
    for (int y = 0; y < 64; y++)
      for (int x = 0; x < 64; x++)
      {
        const float fx = (x - 31.5f) / 31.5f, fy = (y - 31.5f) / 31.5f;
        const float r = std::sqrt(fx * fx + fy * fy);
        const float a = std::atan2(fy, fx);
        const float star = 0.62f * std::pow(std::fabs(std::cos(a * 2.0f)), 6.0f) + 0.2f;
        Uint8 rr = 0, gg = 0, bb = 0, aa = 0;
        if (r < 0.98f)
        {
          rr = 170;
          gg = 12;
          bb = 10;
          aa = 255;
        }
        if (r < star && r > 0.09f)
        {
          rr = 235;
          gg = 230;
          bb = 225;
        }
        px[y * 64 + x] = SDL_MapRGBA(icon->format, rr, gg, bb, aa);
      }
    SDL_SetWindowIcon(m_window, icon);
    SDL_FreeSurface(icon);
  }

  if (!m_audio.Init(Paths::AssetsDir()))
    SDL_Log("La interfaz funcionará sin música ni efectos");
  m_audio.SetVolumes(m_cfg.music_volume, m_cfg.sfx_volume);

  for (int i = 0; i < SDL_NumJoysticks(); i++)
    if (SDL_IsGameController(i))
    {
      OpenController(i);
      break;
    }
  return true;
}

void App::OpenController(int index)
{
  if (m_pad)
    return;
  m_pad = SDL_GameControllerOpen(index);
  m_emu.SetRumbleTarget(m_pad);
}

void App::Shutdown()
{
  StopGame(m_screenshot_path.empty()); // en el modo de prueba no se toca la partida del usuario
  m_audio.Shutdown();
  m_r.Shutdown();
  if (m_pad)
    SDL_GameControllerClose(m_pad);
  if (m_gl)
    SDL_GL_DeleteContext(m_gl);
  if (m_window)
    SDL_DestroyWindow(m_window);
  SDL_Quit();
}

SDL_HitTestResult App::HitTest(SDL_Window* win, const SDL_Point* pt, void* data)
{
  App* app = static_cast<App*>(data);
  const Uint32 flags = SDL_GetWindowFlags(win);
  if (flags & SDL_WINDOW_FULLSCREEN)
    return SDL_HITTEST_NORMAL;

  int w, h;
  SDL_GetWindowSize(win, &w, &h);
  const int border = 6;
  if (!(flags & SDL_WINDOW_MAXIMIZED))
  {
    const bool l = pt->x < border, r = pt->x >= w - border, t = pt->y < border, b = pt->y >= h - border;
    if (t && l) return SDL_HITTEST_RESIZE_TOPLEFT;
    if (t && r) return SDL_HITTEST_RESIZE_TOPRIGHT;
    if (b && l) return SDL_HITTEST_RESIZE_BOTTOMLEFT;
    if (b && r) return SDL_HITTEST_RESIZE_BOTTOMRIGHT;
    if (t) return SDL_HITTEST_RESIZE_TOP;
    if (b) return SDL_HITTEST_RESIZE_BOTTOM;
    if (l) return SDL_HITTEST_RESIZE_LEFT;
    if (r) return SDL_HITTEST_RESIZE_RIGHT;
  }

  int dw, dh;
  SDL_GL_GetDrawableSize(win, &dw, &dh);
  const float px = pt->x * static_cast<float>(dw) / w, py = pt->y * static_cast<float>(dh) / h;
  if (app->m_screen != Screen::Game && py < app->IS(kTitleH))
  {
    if (app->m_tb_min.Contains(px, py) || app->m_tb_max.Contains(px, py) || app->m_tb_close.Contains(px, py))
      return SDL_HITTEST_NORMAL;
    return SDL_HITTEST_DRAGGABLE;
  }
  return SDL_HITTEST_NORMAL;
}

// ------------------------------------------------------------------------------------------------
// Acciones
// ------------------------------------------------------------------------------------------------

void App::ShowDialog(const std::string& text)
{
  m_dialog = text;
  m_modal = Modal::Dialog;
}

void App::ActivateMenu(int index)
{
  if (index >= 1 && index <= 3)
    m_audio.Play(Sfx::Select);
  switch (index)
  {
    case 0:
      if (HasResumeState() && !m_cfg.hardcore)
      {
        m_modal = Modal::PlayChoice;
        m_choice_sel = 0;
      }
      else
      {
        StartGame(false);
      }
      break;
    case 1:
      m_screen = Screen::Achievements;
      m_ach_scroll = m_ach_scroll_target = 0;
      m_ach_sel = 0;
      m_login_focus = 0;
      break;
    case 2:
      m_screen = Screen::Options;
      m_opt_page = -1;
      m_opt_sel = 0;
      break;
    case 3:
      m_screen = Screen::Credits;
      m_credits_scroll = m_credits_target = -60; // empieza un poco más abajo y sube
      break;
    case 4: RequestQuit(); break;
  }
}

void App::SetGamePath(const std::string& path)
{
  m_cfg.game_path = path;
  m_cfg.Save();
}

void App::StartGame(bool resume)
{
  m_modal = Modal::None;
  if (m_cfg.game_path.empty() || !Paths::FileExists(m_cfg.game_path))
  {
    ShowDialog("No hay ninguna imagen de disco seleccionada.\n\nCópiala en la carpeta game/ del launcher,\n"
               "elígela en OPCIONES · ISO y BIOS,\no arrástrala sobre la ventana.");
    return;
  }

  // Hardcore: sólo con la BIOS original de la consola y siempre desde el arranque.
  if (m_cfg.hardcore && !HasSonyBios())
  {
    ShowDialog("El modo hardcore funciona sólo con la BIOS original de tu PlayStation.\n\nCárgala en OPCIONES · "
               "ISO y BIOS, o desactiva allí el modo hardcore.");
    return;
  }
  m_hardcore_session = m_cfg.hardcore;
  if (m_hardcore_session)
    resume = false;

  // Un frame en negro mientras arranca el núcleo.
  m_r.BeginFrame(m_w, m_h, m_time);
  glClearColor(0, 0, 0, 1);
  glClear(GL_COLOR_BUFFER_BIT);
  SDL_GL_SwapWindow(m_window);

  m_audio.Play(Sfx::Start);
  std::vector<std::string> cheats = m_hardcore_session ? std::vector<std::string>{} : ActiveCheatCodes();
  m_cheats_active = !cheats.empty();
  // Bloqueo con la pose de pie con el arma fuera (movimiento 360°); no es un truco: no pausa logros.
  if (!m_hardcore_session)
  {
    m_freecam.SetGame(m_cfg.free_camera ? Paths::ReadDiscSerial(m_cfg.game_path) : "");
    const std::string block = FreeCamera::BlockPatch(Paths::ReadDiscSerial(m_cfg.game_path), ModernControls());
    if (!block.empty())
      cheats.push_back(block);
  }
  m_emu.SetCheats(cheats);
  m_emu.SetButtonPatches(GameButtonPatches());
  // Cámara libre: sólo fuera del modo hardcore (escribe en la RAM del juego).
  m_freecam.SetGame(m_cfg.free_camera && !m_hardcore_session ? Paths::ReadDiscSerial(m_cfg.game_path) : "");
  m_freecam.SetSpeed(m_cfg.camera_speed / 100.0f);
  std::string error;
  if (!m_emu.Start(m_cfg, m_window, m_gl, &error))
  {
    ShowDialog(error);
    return;
  }
  m_emu.SetRumbleTarget(m_pad);
  if (resume && !m_emu.LoadState(StatePath()))
    Toast("No se pudo cargar la partida guardada");

  // Logros: se evalúan tras cada frame emulado leyendo la RAM (en pausa si hay trucos activos).
  if (m_cheats_active)
  {
    m_ach.EndSession();
    Toast("Trucos activos: los logros están en pausa");
  }
  else
  {
    m_ach.BeginSession(m_hardcore_session);
    if (m_hardcore_session)
      Toast("Modo hardcore: sin estados guardados ni trucos");
  }
  m_emu.on_frame = [this]() {
    size_t n = 0;
    const uint8_t* ram = m_emu.SystemRam(&n);
    // Pruebas: TENSHU_TEST_POKE="13f1ce+=8,92580:32=41000" escribe (o suma) palabras de 16 bits
    // (o de 32 con ":32") en la RAM tras cada frame.
    // TENSHU_TEST_POKE_FRAMES=N: sólo durante los N primeros frames.
    const char* poke_frames = std::getenv("TENSHU_TEST_POKE_FRAMES");
    if (const char* poke = std::getenv("TENSHU_TEST_POKE");
        poke && ram && (!poke_frames || m_emu.FramesRun() < static_cast<uint64_t>(std::atoll(poke_frames))))
    {
      std::stringstream ss(poke);
      std::string item;
      while (std::getline(ss, item, ','))
      {
        const size_t eq = item.find('=');
        if (eq == std::string::npos)
          continue;
        const bool add = eq > 0 && item[eq - 1] == '+';
        std::string a = item.substr(0, add ? eq - 1 : eq);
        const bool wide = a.size() > 3 && a.compare(a.size() - 3, 3, ":32") == 0; // "92580:32=..."
        if (wide)
          a.resize(a.size() - 3);
        const uint32_t addr = std::stoul(a, nullptr, 16) & (wide ? 0x1FFFFC : 0x1FFFFE);
        const int v = std::stoi(item.substr(eq + 1));
        if (addr + 4 > n)
          continue;
        uint8_t* p = const_cast<uint8_t*>(ram) + addr;
        if (wide)
        {
          int32_t cur;
          std::memcpy(&cur, p, 4);
          cur = add ? cur + v : v;
          std::memcpy(p, &cur, 4);
        }
        else
        {
          int16_t cur;
          std::memcpy(&cur, p, 2);
          cur = static_cast<int16_t>(add ? cur + v : v);
          std::memcpy(p, &cur, 2);
        }
      }
    }
    // Cámara libre con el joystick derecho (R3 = cámara del juego).
    const bool r3 = m_last_input.buttons & (1u << RETRO_DEVICE_ID_JOYPAD_R3);
    // L3: fijar / soltar enemigo.
    const bool l3 = m_last_input.buttons & (1u << RETRO_DEVICE_ID_JOYPAD_L3);
    if (l3 && !m_l3_was_down)
      m_freecam.RequestLock();
    m_l3_was_down = l3;
    const float dt = static_cast<float>(1.0 / m_emu.TargetFps());
    if (m_look_dx != 0 || m_look_dy != 0)
    {
      // Ratón: 0,0045 rad por píxel con la velocidad de cámara al 100 %.
      const float k = 0.0045f * m_cfg.camera_speed / 100.0f;
      m_freecam.AddLook(m_look_dx * k, m_look_dy * k * 0.8f);
      m_look_dx = m_look_dy = 0;
    }
    m_freecam.Frame(const_cast<uint8_t*>(ram), n, m_last_input.analog[1][0] / 32767.0f,
                    m_last_input.analog[1][1] / 32767.0f, r3 && !m_r3_was_down, ModernControls(), dt);
    if (ModernControls())
      m_freecam.Steer(const_cast<uint8_t*>(ram), n, m_move[0], m_move[1], dt);
    m_r3_was_down = r3;
    // Fijar enemigo: la marca sobre el enemigo (RenderGame) es el único aviso; se calcula aquí, tras
    // el frame, con la cámara que verá el jugador.
    m_freecam.TakeLockEvent();
    m_mark_visible = m_freecam.LockMarker(ram, n, &m_mark_x, &m_mark_y, std::getenv("TENSHU_DEBUG_MARK") != nullptr);
    m_ach.DoFrame(ram, n);
  };

  m_screen = Screen::Game;
  m_paused = false;
  m_pause_sel = 0;
  m_mouse_idle = 0;
}

void App::StopGame(bool autosave)
{
  if (!m_emu.IsRunning())
    return;
  if (autosave && !m_hardcore_session)
    SaveStateNow();
  m_emu.on_frame = nullptr;
  m_ach.EndSession();
  m_emu.Stop();
  SDL_ShowCursor(SDL_ENABLE);
}

void App::ActivatePause(int index)
{
  if (index != 5)
    m_audio.Play(index == 0 ? Sfx::Cancel : Sfx::Select);
  auto resume = [this]() {
    m_paused = false;
    m_emu.SetPaused(false);
  };
  switch (index)
  {
    case 0: resume(); break;
    case 1:
      if (StatesBlocked())
        break;
      Toast(SaveStateNow() ? "Partida guardada" : "No se pudo guardar la partida");
      resume();
      break;
    case 2:
      if (StatesBlocked())
        break;
      if (m_emu.LoadState(StatePath()))
      {
        if (!m_cheats_active)
          m_ach.BeginSession(false); // reinicia la evaluación tras cambiar la memoria
        Toast("Partida cargada");
      }
      else
      {
        Toast("No hay partida guardada");
      }
      resume();
      break;
    case 3: m_screen = Screen::Controls; break;
    case 4:
      StopGame(true);
      m_screen = Screen::Menu;
      m_sel = 0;
      m_fade = 0;
      break;
    case 5: RequestQuit(); break;
  }
}

void App::RequestQuit()
{
  if (m_outro_t >= 0)
    return;
  StopGame(true); // si se sale desde la pausa, guarda y vuelve al fondo del menú
  m_screen = Screen::Menu;
  m_modal = Modal::None;
  m_intro_t = std::max(m_intro_t, kIntroLen);
  m_fade = 1.0f;
  m_outro_t = 0;
  m_audio.Play(Sfx::Close);
}

void App::ToggleFullscreen()
{
  m_cfg.fullscreen = !m_cfg.fullscreen;
  SDL_SetWindowFullscreen(m_window, m_cfg.fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
  m_cfg.Save();
}

void App::ImportBios(const std::string& path)
{
  Paths::MakeDirs(Paths::BiosDir());
  const std::string name = BaseName(path);
  if (Paths::CopyFile(path, Paths::BiosDir() + "/" + name))
  {
    // La BIOS recién cargada queda elegida.
    m_cfg.bios_file = name;
    m_cfg.Save();
    Toast("BIOS añadida: " + name);
  }
  else
  {
    Toast("No se pudo copiar la BIOS");
  }
}

void App::StartLogin()
{
  if (m_login_user.empty() || m_login_pass.empty())
  {
    Toast("Escribe tu usuario y contraseña de RetroAchievements");
    return;
  }
  m_audio.Play(Sfx::Select);
  m_ach.StartDownload(m_login_user, m_login_pass);
  // La contraseña sólo se usa para obtener el token: se borra de memoria al instante.
  std::fill(m_login_pass.begin(), m_login_pass.end(), '\0');
  m_login_pass.clear();
}

void App::OpenOptionPage(int page)
{
  m_audio.Play(Sfx::Select);
  if (page == kCheatsCategory)
  {
    m_screen = Screen::Cheats;
    m_cheat_list = LoadCheats(Paths::AssetsDir(), Paths::ReadDiscSerial(m_cfg.game_path));
    m_cheat_sel = 0;
    m_cheat_scroll = 0;
    return;
  }
  m_opt_page = page;
  m_opt_sel = 0;
}

bool App::CheatEnabled(const std::string& name) const
{
  std::stringstream ss(m_cfg.cheats);
  std::string item;
  while (std::getline(ss, item, '|'))
    if (item == name)
      return true;
  return false;
}

void App::SetCheatEnabled(const std::string& name, bool on)
{
  std::vector<std::string> names;
  std::stringstream ss(m_cfg.cheats);
  std::string item;
  while (std::getline(ss, item, '|'))
    if (!item.empty() && item != name)
      names.push_back(item);
  if (on)
    names.push_back(name);
  m_cfg.cheats.clear();
  for (const std::string& n : names)
    m_cfg.cheats += (m_cfg.cheats.empty() ? "" : "|") + n;
  m_cfg.Save();
}

std::vector<std::string> App::ActiveCheatCodes() const
{
  std::vector<std::string> codes;
  for (const Cheat& c : LoadCheats(Paths::AssetsDir(), Paths::ReadDiscSerial(m_cfg.game_path)))
    if (CheatEnabled(c.name))
      codes.push_back(c.codes);
  return codes;
}

// Recorre OpenBIOS (valor vacío) y las BIOS cargadas en bios/.
void App::CycleBios(int dir)
{
  std::vector<std::string> list = {""};
  for (const std::string& b : Paths::FindBiosFiles())
    list.push_back(b);
  int idx = 0;
  for (size_t i = 0; i < list.size(); i++)
    if (list[i] == m_cfg.bios_file)
      idx = static_cast<int>(i);
  const int n = static_cast<int>(list.size());
  idx = (idx + (dir == 0 ? 1 : dir) + n) % n;
  m_cfg.bios_file = list[idx];
}

void App::OptionChange(int row, int dir)
{
  const bool activate = (dir == 0);
  m_audio.Play(activate ? (row == OPT_BACK ? Sfx::Cancel : Sfx::Select) : Sfx::Move);
  switch (row)
  {
    case OPT_GAME:
      if (!activate)
        return;
      if (!FilePicker::Available())
      {
        ShowDialog("No se encontró zenity ni kdialog.\nArrastra la imagen del disco sobre la ventana.");
        return;
      }
      m_picker.Open("Imagen del disco", "*.cue *.CUE *.chd *.CHD *.bin *.BIN *.iso *.img *.pbp *.m3u",
                    [this](const std::string& p) { SetGamePath(p); });
      return;
    case OPT_BIOS: CycleBios(dir); break;
    case OPT_BIOS_LOAD:
      if (!activate)
        return;
      if (!FilePicker::Available())
      {
        ShowDialog("No se encontró zenity ni kdialog.\nArrastra el archivo de la BIOS (512 KB) sobre la ventana.");
        return;
      }
      m_picker.Open("BIOS de PlayStation", "*.bin *.BIN *.rom *.ROM", [this](const std::string& p) { ImportBios(p); });
      return;
    case OPT_RENDERER: m_cfg.hardware_renderer = !m_cfg.hardware_renderer; break;
    case OPT_SCALE:
    {
      // Escalas verificadas con Tenchu 2. 3x se omite: corrompe el texto del juego.
      static const int scales[] = {1, 2, 4, 5};
      int idx = 0;
      for (int i = 0; i < 4; i++)
        if (scales[i] == m_cfg.resolution_scale)
          idx = i;
      idx = activate ? (idx + 1) % 4 : std::clamp(idx + dir, 0, 3);
      m_cfg.resolution_scale = scales[idx];
      break;
    }
    case OPT_TEXFILTER:
    {
      static const char* filters[] = {"Nearest", "Bilinear", "JINC2", "xBR"};
      int idx = 3;
      for (int i = 0; i < 4; i++)
        if (m_cfg.texture_filter == filters[i])
          idx = i;
      idx = activate ? (idx + 1) % 4 : (idx + dir + 4) % 4;
      m_cfg.texture_filter = filters[idx];
      break;
    }
    case OPT_PGXP:
    {
      // No → Sí → Máxima (con la CPU)
      int level = !m_cfg.pgxp ? 0 : m_cfg.pgxp_cpu ? 2 : 1;
      level = activate ? (level + 1) % 3 : std::clamp(level + dir, 0, 2);
      m_cfg.pgxp = level > 0;
      m_cfg.pgxp_cpu = level == 2;
      break;
    }
    case OPT_WIDESCREEN:
      if (!m_ws_available)
        return;
      m_cfg.widescreen = !m_cfg.widescreen;
      break;
    case OPT_SIDEART: m_cfg.side_art = !m_cfg.side_art; break;
    case OPT_SMOOTH: m_cfg.smooth_filter = !m_cfg.smooth_filter; break;
    case OPT_FULLSCREEN: ToggleFullscreen(); return;
    case OPT_VOLUME:
      m_cfg.volume =
        activate ? (m_cfg.volume >= 100 ? 0 : m_cfg.volume + 10) : std::clamp(m_cfg.volume + dir * 10, 0, 100);
      m_emu.SetVolume(m_cfg.volume);
      break;
    case OPT_MUSIC:
      m_cfg.music_volume = activate ? (m_cfg.music_volume >= 100 ? 0 : m_cfg.music_volume + 10)
                                    : std::clamp(m_cfg.music_volume + dir * 10, 0, 100);
      m_audio.SetVolumes(m_cfg.music_volume, m_cfg.sfx_volume);
      break;
    case OPT_SFX:
      m_cfg.sfx_volume = activate ? (m_cfg.sfx_volume >= 100 ? 0 : m_cfg.sfx_volume + 10)
                                  : std::clamp(m_cfg.sfx_volume + dir * 10, 0, 100);
      m_audio.SetVolumes(m_cfg.music_volume, m_cfg.sfx_volume);
      break;
    case OPT_FASTBOOT: m_cfg.fast_boot = !m_cfg.fast_boot; break;
    case OPT_HARDCORE:
      m_cfg.hardcore = !m_cfg.hardcore;
      if (m_cfg.hardcore && !HasSonyBios())
        ShowDialog("Modo hardcore activado.\n\nNecesita la BIOS original de tu PlayStation: cárgala con "
                   "«Cargar BIOS nueva…» y elígela en la fila BIOS.");
      break;
    case OPT_GAME_PROMPTS: m_cfg.game_prompts = !m_cfg.game_prompts; break;
    case OPT_FREE_CAMERA: m_cfg.free_camera = !m_cfg.free_camera; break;
    case OPT_MOVEMENT: m_cfg.modern_controls = !m_cfg.modern_controls; break;
    case OPT_KB_SCHEME: m_cfg.kb_mouse = !m_cfg.kb_mouse; break;
    case OPT_CAMERA_SPEED:
      m_cfg.camera_speed = activate ? (m_cfg.camera_speed >= 300 ? 20 : m_cfg.camera_speed + 10)
                                    : std::clamp(m_cfg.camera_speed + dir * 10, 20, 300);
      break;
    case OPT_PROMPTS:
    {
      static const char* styles[] = {"auto", "keyboard", "xbox", "playstation"};
      int idx = 0;
      for (int i = 0; i < 4; i++)
        if (m_cfg.prompt_style == styles[i])
          idx = i;
      idx = activate ? (idx + 1) % 4 : (idx + dir + 4) % 4;
      m_cfg.prompt_style = styles[idx];
      break;
    }
    case OPT_CONTROLS:
      if (activate)
        m_screen = Screen::Controls;
      return;
    case OPT_REMAP_KB:
    case OPT_REMAP_PAD:
      if (!activate || (row == OPT_REMAP_KB && m_cfg.kb_mouse))
        return;
      m_screen = Screen::Remap;
      m_remap_device = (row == OPT_REMAP_PAD) ? 1 : 0;
      m_remap_sel = 0;
      m_remap_wait = -1;
      return;
    case OPT_RESET_CONTROLS:
      if (!activate)
        return;
      // Doble pulsación para confirmar: evita borrar la configuración por accidente.
      if (m_reset_confirm > 0)
      {
        m_cfg.DefaultKeyboard();
        m_cfg.DefaultPad();
        m_reset_confirm = 0;
        Toast("Controles restaurados por defecto");
        break;
      }
      m_reset_confirm = 3.0f;
      return;
    case OPT_BACK:
      if (activate)
      {
        m_opt_sel = m_opt_page;
        m_opt_page = -1;
      }
      return;
  }
  m_cfg.Save();
}

// ------------------------------------------------------------------------------------------------
// Entrada
// ------------------------------------------------------------------------------------------------

PromptStyle App::PadStyle() const
{
  const SDL_GameControllerType t = m_pad ? SDL_GameControllerGetType(m_pad) : SDL_CONTROLLER_TYPE_UNKNOWN;
  return (t == SDL_CONTROLLER_TYPE_PS3 || t == SDL_CONTROLLER_TYPE_PS4 || t == SDL_CONTROLLER_TYPE_PS5)
           ? PromptStyle::PlayStation
           : PromptStyle::Xbox;
}

PromptStyle App::CurrentStyle() const
{
  if (m_cfg.prompt_style == "keyboard") return PromptStyle::Keyboard;
  if (m_cfg.prompt_style == "xbox") return PromptStyle::Xbox;
  if (m_cfg.prompt_style == "playstation") return PromptStyle::PlayStation;
  return m_last_device;
}

void App::Navigate(int dy, int dx)
{
  const int before[] = {m_sel, m_opt_sel, m_pause_sel, m_choice_sel, m_login_focus, m_ach_sel, m_remap_sel};
  NavigateImpl(dy, dx);
  const int after[] = {m_sel, m_opt_sel, m_pause_sel, m_choice_sel, m_login_focus, m_ach_sel, m_remap_sel};
  if (!std::equal(std::begin(before), std::end(before), std::begin(after)))
    m_audio.Play(Sfx::Move);
}

void App::NavigateImpl(int dy, int dx)
{
  if (m_modal == Modal::Dialog)
    return;
  if (m_modal == Modal::PlayChoice)
  {
    if (dy)
      m_choice_sel = (m_choice_sel + dy + 3) % 3;
    return;
  }
  if (m_modal == Modal::ConfirmReset)
  {
    if (dy || dx)
      m_choice_sel = 1 - m_choice_sel;
    return;
  }
  switch (m_screen)
  {
    case Screen::Menu: m_sel = (m_sel + dy + kMenuItems) % kMenuItems; break;
    case Screen::Credits: m_credits_target += dy * 120.0f; break;
    case Screen::Cheats:
    {
      // Filas: los trucos, "Desactivar todos" y "Volver".
      const int n = static_cast<int>(m_cheat_list.size()) + 2;
      if (dy)
        m_cheat_sel = (m_cheat_sel + dy + n) % n;
      if (dx && m_cheat_sel < static_cast<int>(m_cheat_list.size()))
        Accept();
      break;
    }
    case Screen::Options:
    {
      const int n = m_opt_page < 0 ? kOptCategoryCount + 1 : static_cast<int>(OptPageRows(m_opt_page).size());
      if (dy)
        m_opt_sel = (m_opt_sel + dy + n) % n;
      if (dx && m_opt_page >= 0)
        OptionChange(OptPageRows(m_opt_page)[m_opt_sel], dx);
      break;
    }
    case Screen::Achievements:
      if (m_ach.GetState() == Achievements::State::NeedLogin)
      {
        if (dy)
          m_login_focus = (m_login_focus + dy + 3) % 3;
      }
      else if (m_ach.GetState() == Achievements::State::Ready && dy)
      {
        // La última fila (índice n) es "Reiniciar mis logros".
        const int n = static_cast<int>(m_ach.List().size());
        m_ach_sel = std::clamp(m_ach_sel + dy, 0, n);
      }
      break;
    case Screen::Remap:
    {
      // Dos columnas de 8 botones y, debajo, "Restaurar" (16) y "Volver" (17).
      int& i = m_remap_sel;
      if (dy)
      {
        if (i < kPsButtons)
        {
          const int col = i / 8, row = i % 8 + dy;
          i = row < 0 ? kPsButtons + 1 : row > 7 ? kPsButtons : col * 8 + row;
        }
        else if (i == kPsButtons)
          i = dy > 0 ? kPsButtons + 1 : 7;
        else
          i = dy > 0 ? 0 : kPsButtons;
      }
      if (dx && i < kPsButtons)
        i = ((i / 8 + 1) % 2) * 8 + i % 8;
      break;
    }
    case Screen::Game:
      if (m_paused)
        m_pause_sel = (m_pause_sel + dy + kPauseItems) % kPauseItems;
      break;
    default: break;
  }
}

void App::Accept()
{
  if (m_modal == Modal::Dialog)
  {
    m_audio.Play(Sfx::Select);
    m_modal = Modal::None;
    return;
  }
  if (m_modal == Modal::ConfirmReset)
  {
    m_modal = Modal::None;
    if (m_choice_sel == 0)
    {
      m_ach.ResetUnlocks();
      m_audio.Play(Sfx::Close);
      Toast("Logros reiniciados");
    }
    else
    {
      m_audio.Play(Sfx::Cancel);
    }
    return;
  }
  if (m_modal == Modal::PlayChoice)
  {
    if (m_choice_sel == 0)
      StartGame(true);
    else if (m_choice_sel == 1)
      StartGame(false);
    else
    {
      m_audio.Play(Sfx::Cancel);
      m_modal = Modal::None;
    }
    return;
  }
  switch (m_screen)
  {
    case Screen::Menu: ActivateMenu(m_sel); break;
    case Screen::Options:
      if (m_opt_page < 0)
      {
        if (m_opt_sel < kOptCategoryCount)
          OpenOptionPage(m_opt_sel);
        else
          Back();
      }
      else
      {
        OptionChange(OptPageRows(m_opt_page)[m_opt_sel], 0);
      }
      break;
    case Screen::Achievements:
      if (m_ach.GetState() == Achievements::State::NeedLogin)
      {
        if (m_login_focus < 2)
          m_login_focus++;
        else
          StartLogin();
      }
      else if (m_ach.GetState() == Achievements::State::Ready &&
               m_ach_sel == static_cast<int>(m_ach.List().size()))
      {
        m_audio.Play(Sfx::Select);
        m_modal = Modal::ConfirmReset;
        m_choice_sel = 1; // por seguridad, empieza en "Cancelar"
      }
      break;
    case Screen::Cheats:
    {
      const int n = static_cast<int>(m_cheat_list.size());
      if (m_cheat_sel < n)
      {
        const Cheat& c = m_cheat_list[m_cheat_sel];
        const bool on = !CheatEnabled(c.name);
        SetCheatEnabled(c.name, on);
        m_audio.Play(on ? Sfx::Select : Sfx::Cancel);
      }
      else if (m_cheat_sel == n)
      {
        m_cfg.cheats.clear();
        m_cfg.Save();
        m_audio.Play(Sfx::Cancel);
        Toast("Trucos desactivados");
      }
      else
      {
        Back();
      }
      break;
    }
    case Screen::Remap:
      if (m_remap_sel < kPsButtons)
      {
        m_audio.Play(Sfx::Select);
        m_remap_wait = kRemapOrder[m_remap_sel];
        m_remap_timer = 6.0f;
      }
      else if (m_remap_sel == kPsButtons)
      {
        m_audio.Play(Sfx::Select);
        if (m_remap_device == 0)
          m_cfg.DefaultKeyboard();
        else
          m_cfg.DefaultPad();
        m_cfg.Save();
        Toast(m_remap_device == 0 ? "Teclado restaurado por defecto" : "Mando restaurado por defecto");
      }
      else
      {
        Back();
      }
      break;
    case Screen::Game:
      if (m_paused)
        ActivatePause(m_pause_sel);
      break;
    default: break;
  }
}

// Asigna `value` (tecla o botón del mando) al botón `retro_id`. Si ya estaba en uso por otro botón
// del mismo dispositivo, se intercambian: nunca quedan dos acciones en la misma tecla.
void App::AssignBinding(int retro_id, int value)
{
  int* table = m_remap_device == 0 ? m_cfg.key_bind : m_cfg.pad_bind;
  const int old = table[retro_id];
  for (int i = 0; i < kPsButtons; i++)
    if (i != retro_id && table[i] == value)
    {
      table[i] = old;
      Toast(std::string(PsButtonName(i)) + " pasa a la asignación anterior de " + PsButtonName(retro_id));
    }
  table[retro_id] = value;
  m_cfg.Save();
  m_remap_wait = -1;
  m_audio.Play(Sfx::Select);
}

void App::Back()
{
  // Abrir la pausa suena como "elegir"; todo lo demás, como "cancelar".
  const bool opening_pause = m_screen == Screen::Game && !m_paused && m_modal == Modal::None;
  m_audio.Play(opening_pause ? Sfx::Select : Sfx::Cancel);
  if (m_modal != Modal::None)
  {
    m_modal = Modal::None;
    return;
  }
  switch (m_screen)
  {
    case Screen::Options:
      if (m_opt_page >= 0)
      {
        m_opt_sel = m_opt_page;
        m_opt_page = -1;
      }
      else
      {
        m_screen = Screen::Menu;
        m_sel = 2;
      }
      break;
    case Screen::Achievements:
      m_screen = Screen::Menu;
      m_sel = 1;
      break;
    case Screen::Credits:
      m_screen = Screen::Menu;
      m_sel = 3;
      break;
    case Screen::Cheats:
      m_screen = Screen::Options;
      m_opt_page = -1;
      m_opt_sel = kCheatsCategory;
      break;
    case Screen::Remap:
      if (m_remap_wait >= 0)
      {
        m_remap_wait = -1;
        break;
      }
      m_screen = Screen::Options;
      m_opt_page = 3;
      m_opt_sel = m_remap_device == 0 ? 1 : 2;
      break;
    case Screen::Controls:
      if (m_emu.IsRunning())
        m_screen = Screen::Game;
      else
      {
        m_screen = Screen::Options;
        m_opt_page = 3;
        m_opt_sel = 1;
      }
      break;
    case Screen::Game:
      m_paused = !m_paused;
      m_pause_sel = 0;
      m_emu.SetPaused(m_paused);
      break;
    default: break;
  }
}

EmuInput App::GatherGameInput()
{
  EmuInput in;
  in.buttons = m_test_buttons;
  // Teclado y mando tienen asignaciones independientes (OPCIONES › Controles).
  const Uint8* k = SDL_GetKeyboardState(nullptr);
  for (int id = 0; id < kPsButtons; id++)
  {
    bool on = false;
    const int key = m_cfg.KeyBind(id);
    if (key > 0 && key < SDL_NUM_SCANCODES && k[key])
      on = true;
    else if (key >= kMouseLeft && m_mouse_captured)
    {
      const Uint32 mb = SDL_GetMouseState(nullptr, nullptr);
      on = (key == kMouseLeft && (mb & SDL_BUTTON_LMASK)) || (key == kMouseRight && (mb & SDL_BUTTON_RMASK)) ||
           (key == kMouseMiddle && (mb & SDL_BUTTON_MMASK)) || (key == kWheelUp && m_wheel_up_t > 0) ||
           (key == kWheelDown && m_wheel_down_t > 0);
    }
    const int pad = m_cfg.pad_bind[id];
    if (m_pad && pad >= kPadAxisBase)
      on |= SDL_GameControllerGetAxis(m_pad, static_cast<SDL_GameControllerAxis>(pad - kPadAxisBase)) > 12000;
    else if (m_pad && pad >= 0)
      on |= SDL_GameControllerGetButton(m_pad, static_cast<SDL_GameControllerButton>(pad)) != 0;
    if (on)
      in.buttons |= static_cast<uint16_t>(1u << id);
  }
  if (m_pad)
  {
    auto ax = [&](SDL_GameControllerAxis a) { return SDL_GameControllerGetAxis(m_pad, a); };
    in.analog[0][0] = ax(SDL_CONTROLLER_AXIS_LEFTX);
    in.analog[0][1] = ax(SDL_CONTROLLER_AXIS_LEFTY);
    in.analog[1][0] = ax(SDL_CONTROLLER_AXIS_RIGHTX);
    in.analog[1][1] = ax(SDL_CONTROLLER_AXIS_RIGHTY);
  }
  if (m_test_lstick[0] != 0 || m_test_lstick[1] != 0)
  {
    in.analog[0][0] = static_cast<int16_t>(m_test_lstick[0] * 32767);
    in.analog[0][1] = static_cast<int16_t>(m_test_lstick[1] * 32767);
  }
  if (m_test_rstick[0] != 0 || m_test_rstick[1] != 0)
  {
    in.analog[1][0] = static_cast<int16_t>(m_test_rstick[0] * 32767);
    in.analog[1][1] = static_cast<int16_t>(m_test_rstick[1] * 32767);
  }
  // Controles 360°: la dirección del joystick izquierdo (o de la cruceta / teclas de dirección) es
  // relativa a la cámara; el personaje se orienta hacia ella (FreeCamera::Steer) y aquí sólo se
  // pide «adelante». Fuera de los modos de movimiento normales llegan los controles originales.
  m_move[0] = m_move[1] = 0;
  if (ModernControls())
  {
    float mx = in.analog[0][0] / 32767.0f, my = in.analog[0][1] / 32767.0f;
    if (mx * mx + my * my < 0.25f * 0.25f)
    {
      const auto bit = [&](unsigned id) { return (in.buttons >> id) & 1 ? 1.0f : 0.0f; };
      mx = bit(RETRO_DEVICE_ID_JOYPAD_RIGHT) - bit(RETRO_DEVICE_ID_JOYPAD_LEFT);
      my = bit(RETRO_DEVICE_ID_JOYPAD_DOWN) - bit(RETRO_DEVICE_ID_JOYPAD_UP);
    }
    m_move[0] = mx;
    m_move[1] = my;
    if (m_freecam.Steerable())
    {
      const uint16_t dirs = (1u << RETRO_DEVICE_ID_JOYPAD_UP) | (1u << RETRO_DEVICE_ID_JOYPAD_DOWN) |
                            (1u << RETRO_DEVICE_ID_JOYPAD_LEFT) | (1u << RETRO_DEVICE_ID_JOYPAD_RIGHT);
      in.buttons &= static_cast<uint16_t>(~dirs);
      in.analog[0][0] = in.analog[0][1] = 0;
      // Hacia adelante; con un enemigo fijado y moviéndose hacia atrás o de lado, «atrás» (guardia
      // mirándolo: FreeCamera::Steer pone la dirección del paso).
      if (mx * mx + my * my >= 0.25f * 0.25f)
        in.buttons |= static_cast<uint16_t>(
          1u << (m_freecam.Strafing() ? RETRO_DEVICE_ID_JOYPAD_DOWN : RETRO_DEVICE_ID_JOYPAD_UP));
    }
  }
  m_last_input = in;
  return in;
}

void App::HandleEvent(const SDL_Event& e)
{
  // Durante el cierre se ignora todo (cerrar la ventana otra vez fuerza la salida inmediata).
  if (m_outro_t >= 0)
  {
    if (e.type == SDL_QUIT)
      m_running = false;
    return;
  }
  // La apertura se salta con cualquier tecla, botón o clic.
  if (IntroActive() &&
      (e.type == SDL_KEYDOWN || e.type == SDL_CONTROLLERBUTTONDOWN || e.type == SDL_MOUSEBUTTONDOWN))
  {
    m_intro_t = kIntroLen;
    m_fade = 1.0f;
    return;
  }

  // Esperando una tecla/botón nuevo en la pantalla de personalización.
  if (m_remap_wait >= 0)
  {
    if (e.type == SDL_KEYDOWN && !e.key.repeat)
    {
      const SDL_Scancode sc = e.key.keysym.scancode;
      if (sc == SDL_SCANCODE_ESCAPE)
      {
        m_audio.Play(Sfx::Cancel);
        m_remap_wait = -1;
      }
      else if (m_remap_device == 0)
      {
        if (IsReservedKey(sc))
          Toast("Esa tecla está reservada por el launcher");
        else
          AssignBinding(m_remap_wait, sc);
      }
      return;
    }
    if (m_remap_device == 1 && e.type == SDL_CONTROLLERBUTTONDOWN)
    {
      if (e.cbutton.button == SDL_CONTROLLER_BUTTON_GUIDE)
        Toast("El botón central está reservado para la pausa");
      else
        AssignBinding(m_remap_wait, e.cbutton.button);
      return;
    }
    if (m_remap_device == 1 && e.type == SDL_CONTROLLERAXISMOTION && e.caxis.value > 20000 &&
        (e.caxis.axis == SDL_CONTROLLER_AXIS_TRIGGERLEFT || e.caxis.axis == SDL_CONTROLLER_AXIS_TRIGGERRIGHT))
    {
      AssignBinding(m_remap_wait, kPadAxisBase + e.caxis.axis);
      return;
    }
    if (e.type == SDL_MOUSEBUTTONDOWN)
    {
      m_audio.Play(Sfx::Cancel);
      m_remap_wait = -1;
      return;
    }
    if (e.type == SDL_CONTROLLERBUTTONDOWN || e.type == SDL_CONTROLLERAXISMOTION || e.type == SDL_KEYDOWN ||
        e.type == SDL_TEXTINPUT)
      return;
  }

  switch (e.type)
  {
    case SDL_QUIT: RequestQuit(); break;

    case SDL_DROPFILE:
    {
      const std::string path = e.drop.file;
      SDL_free(e.drop.file);
      FILE* f = Platform::OpenFile(path.c_str(), "rb");
      long size = 0;
      if (f)
      {
        std::fseek(f, 0, SEEK_END);
        size = std::ftell(f);
        std::fclose(f);
      }
      if (size == 512 * 1024)
        ImportBios(path);
      else if (IsDiscImage(path))
      {
        SetGamePath(path);
        Toast("Juego: " + BaseName(path));
      }
      else
        Toast("Archivo no reconocido");
      break;
    }

    case SDL_CONTROLLERDEVICEADDED: OpenController(e.cdevice.which); break;
    case SDL_CONTROLLERDEVICEREMOVED:
      if (m_pad && SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(m_pad)) == e.cdevice.which)
      {
        SDL_GameControllerClose(m_pad);
        m_pad = nullptr;
        m_emu.SetRumbleTarget(nullptr);
        m_last_device = PromptStyle::Keyboard;
        if (m_screen == Screen::Game && !m_paused)
          Back(); // pausa automática al desconectar el mando
      }
      break;

    case SDL_TEXTINPUT:
      if (TextFocus())
      {
        std::string& field = m_login_focus == 0 ? m_login_user : m_login_pass;
        if (field.size() < 64)
          field += e.text.text;
      }
      break;

    case SDL_KEYDOWN:
    {
      m_last_device = PromptStyle::Keyboard;
      const SDL_Keycode key = e.key.keysym.sym;
      if (key == SDLK_F11 || (key == SDLK_RETURN && (e.key.keysym.mod & KMOD_ALT)))
      {
        ToggleFullscreen();
        break;
      }
      if (key == SDLK_F9 && !e.key.repeat)
      {
        std::string err;
        Toast(m_r.ReloadShaders(&err) ? "Shaders recargados" : "Error en shaders");
        if (!err.empty())
          SDL_Log("%s", err.c_str());
        break;
      }
      // Edición de texto en el formulario de logros.
      if (TextFocus())
      {
        std::string& field = m_login_focus == 0 ? m_login_user : m_login_pass;
        if (key == SDLK_BACKSPACE)
        {
          PopUtf8(field);
          break;
        }
        if (key == SDLK_v && (e.key.keysym.mod & KMOD_CTRL))
        {
          if (char* clip = SDL_GetClipboardText())
          {
            field += clip;
            SDL_free(clip);
          }
          break;
        }
        if (key == SDLK_TAB)
        {
          m_login_focus = (m_login_focus + 1) % 3;
          break;
        }
        if (key != SDLK_RETURN && key != SDLK_KP_ENTER && key != SDLK_ESCAPE && key != SDLK_UP && key != SDLK_DOWN)
          break; // el resto de teclas escriben texto (SDL_TEXTINPUT)
      }
      if (m_screen == Screen::Game && !m_paused && m_modal == Modal::None)
      {
        if (key == SDLK_ESCAPE || key == SDLK_F1)
          Back();
        else if (key == SDLK_F12)
        {
          // Diagnóstico: volcado de la RAM de la consola (para revisar logros que no saltan).
          size_t n = 0;
          if (const uint8_t* ram = m_emu.SystemRam(&n))
          {
            const std::string dir = Paths::DataDir() + "/diagnostico";
            Paths::MakeDirs(dir);
            char name[64];
            const time_t t = std::time(nullptr);
            std::strftime(name, sizeof(name), "/ram-%Y%m%d-%H%M%S.bin", std::localtime(&t));
            if (FILE* f = Platform::OpenFile((dir + name).c_str(), "wb"))
            {
              std::fwrite(ram, 1, n, f);
              std::fclose(f);
              Toast("RAM guardada para diagnóstico");
            }
          }
        }
        else if ((key == SDLK_F5 || key == SDLK_F7) && StatesBlocked())
        {
        }
        else if (key == SDLK_F5)
          Toast(SaveStateNow() ? "Partida guardada" : "No se pudo guardar");
        else if (key == SDLK_F7)
        {
          if (m_emu.LoadState(StatePath()))
          {
            if (!m_cheats_active)
              m_ach.BeginSession(false);
            Toast("Partida cargada");
          }
          else
          {
            Toast("No hay partida guardada");
          }
        }
        break;
      }
      switch (key)
      {
        case SDLK_UP: Navigate(-1, 0); break;
        case SDLK_DOWN: Navigate(1, 0); break;
        case SDLK_LEFT: Navigate(0, -1); break;
        case SDLK_RIGHT: Navigate(0, 1); break;
        case SDLK_RETURN:
        case SDLK_KP_ENTER:
        case SDLK_SPACE: Accept(); break;
        case SDLK_ESCAPE:
        case SDLK_BACKSPACE:
          if (m_screen == Screen::Menu && m_modal == Modal::None)
            break;
          Back();
          break;
        default: break;
      }
      break;
    }

    case SDL_CONTROLLERAXISMOTION:
      if (std::abs(e.caxis.value) > 16000)
        m_last_device = PadStyle();
      break;

    case SDL_CONTROLLERBUTTONDOWN:
    {
      m_last_device = PadStyle();
      const auto btn = e.cbutton.button;
      // L3 + R3 a la vez (o el botón central) abre y cierra la pausa durante el juego.
      const bool sticks_combo =
        m_pad &&
        ((btn == SDL_CONTROLLER_BUTTON_LEFTSTICK && SDL_GameControllerGetButton(m_pad, SDL_CONTROLLER_BUTTON_RIGHTSTICK)) ||
         (btn == SDL_CONTROLLER_BUTTON_RIGHTSTICK && SDL_GameControllerGetButton(m_pad, SDL_CONTROLLER_BUTTON_LEFTSTICK)));
      if (m_screen == Screen::Game && m_modal == Modal::None && (sticks_combo || btn == SDL_CONTROLLER_BUTTON_GUIDE))
      {
        Back();
        break;
      }
      if (m_screen == Screen::Game && !m_paused && m_modal == Modal::None)
        break;
      switch (btn)
      {
        case SDL_CONTROLLER_BUTTON_DPAD_UP: Navigate(-1, 0); break;
        case SDL_CONTROLLER_BUTTON_DPAD_DOWN: Navigate(1, 0); break;
        case SDL_CONTROLLER_BUTTON_DPAD_LEFT: Navigate(0, -1); break;
        case SDL_CONTROLLER_BUTTON_DPAD_RIGHT: Navigate(0, 1); break;
        case SDL_CONTROLLER_BUTTON_A:
        case SDL_CONTROLLER_BUTTON_START: Accept(); break;
        case SDL_CONTROLLER_BUTTON_B:
        case SDL_CONTROLLER_BUTTON_GUIDE:
          if (m_screen == Screen::Menu && m_modal == Modal::None)
            break;
          Back();
          break;
        default: break;
      }
      break;
    }

    case SDL_MOUSEWHEEL:
      if (m_mouse_captured)
      {
        // En partida: la rueda cambia de herramienta (L2 / R2 durante un instante).
        (e.wheel.y > 0 ? m_wheel_up_t : m_wheel_down_t) = 0.12f;
        m_last_device = PromptStyle::Keyboard;
        break;
      }
      if (m_screen == Screen::Achievements)
      {
        const int n = static_cast<int>(m_ach.List().size());
        m_ach_sel = std::clamp(m_ach_sel - e.wheel.y, 0, n);
      }
      else if (m_screen == Screen::Credits)
        m_credits_target -= e.wheel.y * 90.0f;
      else if (m_screen == Screen::Cheats)
        Navigate(e.wheel.y > 0 ? -1 : 1, 0);
      else if (m_screen == Screen::Options)
        Navigate(e.wheel.y > 0 ? -1 : 1, 0);
      break;

    case SDL_MOUSEMOTION:
    {
      if (m_mouse_captured)
      {
        m_look_dx += static_cast<float>(e.motion.xrel);
        m_look_dy += static_cast<float>(e.motion.yrel);
        if (e.motion.xrel || e.motion.yrel)
          m_last_device = PromptStyle::Keyboard;
        break;
      }
      int dw, dh, ww, wh;
      SDL_GL_GetDrawableSize(m_window, &dw, &dh);
      SDL_GetWindowSize(m_window, &ww, &wh);
      m_mouse_px = e.motion.x * static_cast<float>(dw) / ww;
      m_mouse_py = e.motion.y * static_cast<float>(dh) / wh;
      m_mouse_idle = 0;
      if (m_screen == Screen::Game)
        SDL_ShowCursor(SDL_ENABLE);
      const int before[] = {m_sel, m_opt_sel, m_pause_sel, m_choice_sel};
      for (const HotZone& z : m_hot)
      {
        if (!z.r.Contains(m_mouse_px, m_mouse_py))
          continue;
        switch (z.group)
        {
          case 0: m_sel = z.index; break;
          case 1: m_opt_sel = z.index; break;
          case 2: m_pause_sel = z.index; break;
          case 3: m_choice_sel = z.index; break;
          case 7: m_remap_sel = z.index; break;
          case 8: m_cheat_sel = z.index; break;
          default: break;
        }
      }
      const int after[] = {m_sel, m_opt_sel, m_pause_sel, m_choice_sel};
      if (!std::equal(std::begin(before), std::end(before), std::begin(after)))
        m_audio.Play(Sfx::Move);
      break;
    }

    case SDL_MOUSEBUTTONDOWN:
    {
      if (m_mouse_captured)
      {
        m_last_device = PromptStyle::Keyboard; // los clics son botones del juego
        break;
      }
      if (e.button.button == SDL_BUTTON_RIGHT)
      {
        if (!(m_screen == Screen::Menu && m_modal == Modal::None))
          Back();
        break;
      }
      if (e.button.button != SDL_BUTTON_LEFT)
        break;
      if (m_modal == Modal::Dialog)
      {
        m_audio.Play(Sfx::Select);
        m_modal = Modal::None;
        break;
      }
      // En orden inverso: lo último dibujado está encima.
      const std::vector<HotZone> zones = m_hot;
      for (auto it = zones.rbegin(); it != zones.rend(); ++it)
        if (it->r.Contains(m_mouse_px, m_mouse_py) && it->click)
        {
          it->click(m_mouse_px, m_mouse_py);
          break;
        }
      break;
    }

    case SDL_WINDOWEVENT:
      if (e.window.event == SDL_WINDOWEVENT_FOCUS_LOST && m_screen == Screen::Game && !m_paused)
        Back(); // pausa al perder el foco
      break;

    default: break;
  }
}

// ------------------------------------------------------------------------------------------------
// Bucle principal
// ------------------------------------------------------------------------------------------------

void App::Update(float dt)
{
  m_time += dt;
  // Esquema teclado y ratón: el ratón se captura en partida (sin pausa ni diálogos, con la
  // ventana activa) para manejar la cámara.
  {
    const bool focused = (SDL_GetWindowFlags(m_window) & SDL_WINDOW_INPUT_FOCUS) != 0;
    const bool want = m_cfg.kb_mouse && m_screen == Screen::Game && m_emu.IsRunning() && !m_paused &&
                      m_modal == Modal::None && focused && m_screenshot_path.empty();
    if (want != m_mouse_captured)
    {
      SDL_SetRelativeMouseMode(want ? SDL_TRUE : SDL_FALSE);
      m_mouse_captured = want;
      m_look_dx = m_look_dy = 0;
    }
    m_wheel_up_t = std::max(0.0f, m_wheel_up_t - dt);
    m_wheel_down_t = std::max(0.0f, m_wheel_down_t - dt);
  }
  if (m_emu.IsRunning())
    m_emu.SetButtonPatches(GameButtonPatches()); // sólo actúa si cambia el mando o la asignación
  m_fade = std::min(1.0f, m_fade + dt * 1.2f);
  m_intro_t += dt;
  if (!m_intro_sound && m_intro_t > 0.12f)
  {
    m_intro_sound = true;
    if (IntroActive())
      m_audio.Play(Sfx::Start);
  }
  if (m_outro_t >= 0)
  {
    m_outro_t += dt;
    if (m_outro_t >= kOutroLen)
      m_running = false;
  }
  m_toast_time = std::max(0.0f, m_toast_time - dt);
  m_reset_confirm = std::max(0.0f, m_reset_confirm - dt);
  if (m_remap_wait >= 0)
  {
    m_remap_timer -= dt;
    if (m_remap_timer <= 0 || m_screen != Screen::Remap)
    {
      m_remap_wait = -1;
      Toast("Sin cambios");
    }
  }
  m_picker.Poll();
  m_ach.Poll();
  // Música: plena en los menús, baja en la pausa y en silencio mientras se juega.
  if (m_outro_t >= 0)
    m_audio.SetMusicLevel(0.0f);
  else if (m_emu.IsRunning())
    m_audio.SetMusicLevel(m_paused || m_screen == Screen::Controls ? 0.45f : 0.0f);
  else
    m_audio.SetMusicLevel(1.0f);
  if (m_cfg.game_path != m_ach_game_path)
  {
    m_ach_game_path = m_cfg.game_path;
    m_ach.SetGame(m_cfg.game_path);
  }
  if (m_cfg.game_path != m_ws_serial_path)
  {
    m_ws_serial_path = m_cfg.game_path;
    m_ws_available = Emulator::WidescreenPatch(Paths::ReadDiscSerial(m_cfg.game_path)) != nullptr;
  }

  // Entrada de texto sólo mientras se edita un campo.
  if (TextFocus() && !SDL_IsTextInputActive())
    SDL_StartTextInput();
  else if (!TextFocus() && SDL_IsTextInputActive())
    SDL_StopTextInput();

  // Navegación con el stick analógico en menús.
  if (m_pad && (m_screen != Screen::Game || m_paused))
  {
    const Sint16 y = SDL_GameControllerGetAxis(m_pad, SDL_CONTROLLER_AXIS_LEFTY);
    const Sint16 x = SDL_GameControllerGetAxis(m_pad, SDL_CONTROLLER_AXIS_LEFTX);
    m_axis_repeat -= dt;
    if (std::abs(y) > 20000 || std::abs(x) > 20000)
    {
      if (m_axis_repeat <= 0)
      {
        if (std::abs(y) > std::abs(x))
          Navigate(y > 0 ? 1 : -1, 0);
        else
          Navigate(0, x > 0 ? 1 : -1);
        m_axis_repeat = 0.22f;
      }
    }
    else
    {
      m_axis_repeat = 0;
    }
  }

  int active = -1;
  if (m_modal == Modal::PlayChoice || m_modal == Modal::ConfirmReset)
    active = m_choice_sel;
  else if (m_screen == Screen::Menu)
    active = m_sel;
  else if (m_screen == Screen::Game)
    active = m_pause_sel;
  else if (m_screen == Screen::Achievements)
    active = m_login_focus;
  else if (m_screen == Screen::Options && m_opt_page < 0)
    active = m_opt_sel;
  for (int i = 0; i < 8; i++)
  {
    const float target = (i == active) ? 1.0f : 0.0f;
    m_sel_anim[i] += (target - m_sel_anim[i]) * std::min(1.0f, dt * 14.0f);
  }

  // Desplazamiento suave de la lista de logros siguiendo la selección.
  if (m_screen == Screen::Achievements && m_ach.GetState() == Achievements::State::Ready)
  {
    const float row_h = 88.0f, view_h = 700.0f;
    const float sel_y = m_ach_sel * row_h;
    if (sel_y < m_ach_scroll_target)
      m_ach_scroll_target = sel_y;
    if (sel_y + row_h > m_ach_scroll_target + view_h)
      m_ach_scroll_target = sel_y + row_h - view_h;
    const float max_scroll = std::max(0.0f, (m_ach.List().size() + 1) * row_h - view_h);
    m_ach_scroll_target = std::clamp(m_ach_scroll_target, 0.0f, max_scroll);
    m_ach_scroll += (m_ach_scroll_target - m_ach_scroll) * std::min(1.0f, dt * 12.0f);
  }

  // Trucos: la fila elegida queda siempre a la vista.
  if (m_screen == Screen::Cheats)
  {
    const float rh = 56.0f, view_h = 560.0f;
    const int n = static_cast<int>(m_cheat_list.size());
    if (m_cheat_sel < n)
    {
      const float target = std::clamp(m_cheat_sel * rh - view_h / 2 + rh, 0.0f, std::max(0.0f, n * rh - view_h));
      m_cheat_scroll += (target - m_cheat_scroll) * std::min(1.0f, dt * 12.0f);
    }
  }

  // Créditos: avanzan solos despacio; las flechas o la rueda los mueven a mano.
  if (m_screen == Screen::Credits)
  {
    m_credits_target += dt * 32.0f;
    if (m_credits_height > 0 && m_credits_target > m_credits_height)
      m_credits_target = -60; // al terminar, vuelve a empezar
    m_credits_target = std::max(m_credits_target, -60.0f);
    m_credits_scroll += (m_credits_target - m_credits_scroll) * std::min(1.0f, dt * 8.0f);
  }

  if (m_screen == Screen::Game)
  {
    m_mouse_idle += dt;
    if (m_mouse_idle > 2.0f && !m_paused)
      SDL_ShowCursor(SDL_DISABLE);
    m_emu.Tick(GatherGameInput());
    for (uint32_t id : m_ach.TakeNewUnlocks())
    {
      m_popups.push_back({id, 6.0f});
      m_audio.Play(Sfx::Achievement);
    }
  }
  for (Popup& p : m_popups)
    p.time -= dt;
  m_popups.erase(std::remove_if(m_popups.begin(), m_popups.end(), [](const Popup& p) { return p.time <= 0; }),
                 m_popups.end());
}

void App::Layout()
{
  SDL_GL_GetDrawableSize(m_window, &m_w, &m_h);
  // La interfaz (logo, botones, lema) usa la altura de la ventana y se ancla a la izquierda.
  m_s = m_h / kImgH;
  m_ox = 0;
  m_oy = 0;
  // El fondo cubre toda la ventana sin bordes, recortando la franja superior de la ilustración
  // (donde estaba la barra de título pintada). Se ancla a la derecha para no perder a los
  // personajes y, si sobra altura, se recorta por igual arriba y abajo.
  const float visible_h = kImgH - kTitleH;
  const float sb = std::max(m_w / kImgW, m_h / visible_h);
  m_bg_rect.w = kImgW * sb;
  m_bg_rect.h = kImgH * sb;
  m_bg_rect.x = m_w - m_bg_rect.w;
  m_bg_rect.y = -kTitleH * sb - std::max(0.0f, (visible_h * sb - m_h) / 2);
}

void App::Render()
{
  Layout();
  m_hot.clear();
  m_r.BeginFrame(m_w, m_h, m_time);
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  glClearColor(0, 0, 0, 1);
  glClear(GL_COLOR_BUFFER_BIT);

  if (m_emu.IsRunning() && (m_screen == Screen::Game || m_screen == Screen::Controls))
  {
    RenderGame();
    if (m_screen == Screen::Controls)
      RenderControls();
    else if (m_paused)
      RenderPause();
    RenderPopups();
  }
  else
  {
    m_r.DrawBackground(m_bg, m_fire, m_bg_rect.x, m_bg_rect.y, m_bg_rect.w, m_bg_rect.h, 0.3f + 0.7f * m_fade);
    switch (m_screen)
    {
      case Screen::Menu: RenderMenu(); break;
      case Screen::Options: RenderOptions(); break;
      case Screen::Controls: RenderControls(); break;
      case Screen::Remap: RenderRemap(); break;
      case Screen::Credits: RenderCredits(); break;
      case Screen::Cheats: RenderCheats(); break;
      case Screen::Achievements: RenderAchievements(); break;
      default: break;
    }
    if (m_modal == Modal::PlayChoice)
      RenderPlayChoice();
    if (m_modal == Modal::ConfirmReset)
      RenderConfirmReset();
    RenderTitleBar();
    RenderTransition();
  }
  if (m_modal == Modal::Dialog)
    RenderDialog();
  RenderToast();
}

// ------------------------------------------------------------------------------------------------
// Elementos comunes
// ------------------------------------------------------------------------------------------------

void App::Panel(float x, float y, float w, float h)
{
  m_r.Rect(IX(x), IY(y), IS(w), IS(h), UiMode::Panel, {0.035f, 0.025f, 0.025f, 0.92f}, {0.75f, 0.06f, 0.05f, 0.95f});
}

void App::DimScreen(float a)
{
  m_r.Rect(0, 0, static_cast<float>(m_w), static_cast<float>(m_h), UiMode::Solid, {0, 0, 0, a});
}

std::vector<std::string> App::Wrap(const Font& f, const std::string& text, float size, float max_w)
{
  std::vector<std::string> lines;
  for (const std::string& para : SplitLines(text))
  {
    std::stringstream ss(para);
    std::string word, line;
    while (ss >> word)
    {
      const std::string test = line.empty() ? word : line + " " + word;
      if (!line.empty() && m_r.TextWidth(f, test, size) > max_w)
      {
        lines.push_back(line);
        line = word;
      }
      else
      {
        line = test;
      }
    }
    lines.push_back(line);
  }
  return lines;
}

std::string App::Truncate(const Font& f, const std::string& text, float size, float max_w)
{
  if (m_r.TextWidth(f, text, size) <= max_w)
    return text;
  std::string t = text;
  while (!t.empty() && m_r.TextWidth(f, t + "…", size) > max_w)
    PopUtf8(t);
  while (!t.empty() && t.back() == ' ')
    t.pop_back();
  return t + "…";
}

void App::FitText(const Font& f, const std::string& text, float x, float y, float max_w, float size, Color c,
                  Align align, bool scroll, int key)
{
  const float w = m_r.TextWidth(f, text, size);
  if (w <= max_w)
  {
    const float tx = align == Align::Left ? x : align == Align::Right ? x + max_w - w : x + (max_w - w) / 2;
    m_r.Text(f, text, tx, y, size, c);
    return;
  }
  if (!scroll)
  {
    m_r.Text(f, Truncate(f, text, size, max_w), x, y, size, c);
    return;
  }
  // Marquesina: pausa al inicio, avanza hasta mostrar el final, pausa y vuelve a empezar.
  if (key != m_marquee_key)
  {
    m_marquee_key = key;
    m_marquee_start = m_time;
  }
  const float overflow = w - max_w;
  const float speed = IS(70);
  const float pause = 1.3f;
  const float cycle = pause + overflow / speed + pause;
  const float t = std::fmod(m_time - m_marquee_start, cycle);
  const float off = std::clamp((t - pause) * speed, 0.0f, overflow);
  m_r.SetClip(x, y - size * 0.25f, max_w, size * 1.7f);
  m_r.Text(f, text, x - off, y, size, c);
  m_r.ClearClip();
}

// Dibuja el icono de un botón según el estilo (teclado, Xbox o PlayStation) y devuelve su ancho.
float App::DrawGlyph(PsButton b, float x, float y, float h, PromptStyle style, bool draw)
{
  struct G
  {
    int kind; // ver padGlyph() en ui.frag
    Color color;
    std::string text;
  };
  const Color psBlue = {0.49f, 0.66f, 0.9f, 1}, psRed = {0.93f, 0.42f, 0.42f, 1};
  const Color psPink = {0.86f, 0.55f, 0.84f, 1}, psGreen = {0.32f, 0.78f, 0.66f, 1};
  const Color xA = {0.38f, 0.72f, 0.22f, 1}, xB = {0.85f, 0.2f, 0.18f, 1};
  const Color xX = {0.15f, 0.45f, 0.86f, 1}, xY = {0.95f, 0.72f, 0.1f, 1};
  const Color key = {0.85f, 0.83f, 0.8f, 1};

  // Botón de PS1 -> índice de asignación (los de juego); -1 para acciones fijas de la interfaz.
  auto retro_id = [](PsButton pb) -> int {
    switch (pb)
    {
      case PsButton::Cross: return RETRO_DEVICE_ID_JOYPAD_B;
      case PsButton::Circle: return RETRO_DEVICE_ID_JOYPAD_A;
      case PsButton::Square: return RETRO_DEVICE_ID_JOYPAD_Y;
      case PsButton::Triangle: return RETRO_DEVICE_ID_JOYPAD_X;
      case PsButton::L1: return RETRO_DEVICE_ID_JOYPAD_L;
      case PsButton::R1: return RETRO_DEVICE_ID_JOYPAD_R;
      case PsButton::L2: return RETRO_DEVICE_ID_JOYPAD_L2;
      case PsButton::R2: return RETRO_DEVICE_ID_JOYPAD_R2;
      case PsButton::L3: return RETRO_DEVICE_ID_JOYPAD_L3;
      case PsButton::R3: return RETRO_DEVICE_ID_JOYPAD_R3;
      case PsButton::Start: return RETRO_DEVICE_ID_JOYPAD_START;
      case PsButton::Select: return RETRO_DEVICE_ID_JOYPAD_SELECT;
      case PsButton::DUp: return RETRO_DEVICE_ID_JOYPAD_UP;
      case PsButton::DDown: return RETRO_DEVICE_ID_JOYPAD_DOWN;
      case PsButton::DLeft: return RETRO_DEVICE_ID_JOYPAD_LEFT;
      case PsButton::DRight: return RETRO_DEVICE_ID_JOYPAD_RIGHT;
      default: return -1;
    }
  };
  // Icono de un botón físico del mando según el estilo (Xbox o PlayStation).
  auto pad_glyph = [&](int v) -> G {
    const bool ps = (style == PromptStyle::PlayStation);
    if (v >= kPadAxisBase)
    {
      const bool left = (v - kPadAxisBase) == SDL_CONTROLLER_AXIS_TRIGGERLEFT;
      return {6, key, ps ? (left ? "L2" : "R2") : (left ? "LT" : "RT")};
    }
    switch (v)
    {
      case SDL_CONTROLLER_BUTTON_A: return ps ? G{2, psBlue, ""} : G{1, xA, "A"};
      case SDL_CONTROLLER_BUTTON_B: return ps ? G{3, psRed, ""} : G{1, xB, "B"};
      case SDL_CONTROLLER_BUTTON_X: return ps ? G{4, psPink, ""} : G{1, xX, "X"};
      case SDL_CONTROLLER_BUTTON_Y: return ps ? G{5, psGreen, ""} : G{1, xY, "Y"};
      case SDL_CONTROLLER_BUTTON_BACK: return {6, key, ps ? "Select" : "Vista"};
      case SDL_CONTROLLER_BUTTON_GUIDE: return {6, key, ps ? "PS" : "Xbox"};
      case SDL_CONTROLLER_BUTTON_START: return {6, key, ps ? "Start" : "Menú"};
      case SDL_CONTROLLER_BUTTON_LEFTSTICK: return {6, key, ps ? "L3" : "LS"};
      case SDL_CONTROLLER_BUTTON_RIGHTSTICK: return {6, key, ps ? "R3" : "RS"};
      case SDL_CONTROLLER_BUTTON_LEFTSHOULDER: return {6, key, ps ? "L1" : "LB"};
      case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER: return {6, key, ps ? "R1" : "RB"};
      case SDL_CONTROLLER_BUTTON_DPAD_UP: return {6, key, "Cruceta arriba"};
      case SDL_CONTROLLER_BUTTON_DPAD_DOWN: return {6, key, "Cruceta abajo"};
      case SDL_CONTROLLER_BUTTON_DPAD_LEFT: return {6, key, "Cruceta izq."};
      case SDL_CONTROLLER_BUTTON_DPAD_RIGHT: return {6, key, "Cruceta der."};
      case SDL_CONTROLLER_BUTTON_MISC1: return {6, key, ps ? "Micro" : "Compartir"};
      case SDL_CONTROLLER_BUTTON_PADDLE1: return {6, key, "P1"};
      case SDL_CONTROLLER_BUTTON_PADDLE2: return {6, key, "P2"};
      case SDL_CONTROLLER_BUTTON_PADDLE3: return {6, key, "P3"};
      case SDL_CONTROLLER_BUTTON_PADDLE4: return {6, key, "P4"};
      case SDL_CONTROLLER_BUTTON_TOUCHPAD: return {6, key, "Panel táctil"};
      default: return {6, key, "—"};
    }
  };
  const int dpad_ids[4] = {RETRO_DEVICE_ID_JOYPAD_UP, RETRO_DEVICE_ID_JOYPAD_DOWN, RETRO_DEVICE_ID_JOYPAD_LEFT,
                           RETRO_DEVICE_ID_JOYPAD_RIGHT};

  G g{0, key, "?"};
  if (style == PromptStyle::Keyboard)
  {
    if (retro_id(b) >= 0)
      g.text = KeyLabel(m_cfg.KeyBind(retro_id(b)));
    else
      switch (b)
      {
        case PsButton::DPad:
        {
          const int arrows[4] = {SDL_SCANCODE_UP, SDL_SCANCODE_DOWN, SDL_SCANCODE_LEFT, SDL_SCANCODE_RIGHT};
          bool are_arrows = true;
          std::string joined;
          for (int i = 0; i < 4; i++)
          {
            are_arrows &= m_cfg.KeyBind(dpad_ids[i]) == arrows[i];
            joined += (i ? " " : "") + KeyLabel(m_cfg.KeyBind(dpad_ids[i]));
          }
          g.text = m_cfg.kb_mouse ? "WASD" : are_arrows ? "Flechas" : joined;
          break;
        }
        case PsButton::Accept: g.text = "Enter"; break;
        case PsButton::Back:
        case PsButton::Pause: g.text = "Esc"; break;
        case PsButton::LStick: g.text = m_cfg.kb_mouse ? "WASD" : "—"; break;
        case PsButton::RStick: g.text = m_cfg.kb_mouse ? "Ratón" : "—"; break;
        default: g.text = "—"; break;
      }
  }
  else
  {
    // En los menús siempre se acepta con el botón de abajo y se vuelve con el de la derecha.
    if (b == PsButton::Accept)
      g = pad_glyph(SDL_CONTROLLER_BUTTON_A);
    else if (b == PsButton::Back)
      g = pad_glyph(SDL_CONTROLLER_BUTTON_B);
    else if (retro_id(b) >= 0)
      g = pad_glyph(m_cfg.pad_bind[retro_id(b)]);
    else
    {
      const bool ps = (style == PromptStyle::PlayStation);
      switch (b)
      {
        case PsButton::DPad:
        {
          bool is_dpad = true;
          const int dpad[4] = {SDL_CONTROLLER_BUTTON_DPAD_UP, SDL_CONTROLLER_BUTTON_DPAD_DOWN,
                               SDL_CONTROLLER_BUTTON_DPAD_LEFT, SDL_CONTROLLER_BUTTON_DPAD_RIGHT};
          for (int i = 0; i < 4; i++)
            is_dpad &= m_cfg.pad_bind[dpad_ids[i]] == dpad[i];
          g = {6, key, is_dpad ? "Cruceta" : "Personalizada"};
          break;
        }
        case PsButton::LStick: g = {6, key, "Stick izq."}; break;
        case PsButton::RStick: g = {6, key, "Stick der."}; break;
        case PsButton::Pause: g = {6, key, ps ? "L3 + R3  /  PS" : "LS + RS  /  Xbox"}; break;
        default: break;
      }
    }
  }

  const float fs = h * 0.58f;
  float w = h;
  if (g.kind == 0 || g.kind == 6)
    w = std::max(h, m_r.TextWidth(m_r.ui, g.text, fs) + h * 0.75f);
  if (!draw)
    return w;
  m_r.Rect(x, y, w, h, UiMode::PadGlyph, g.color, {}, static_cast<float>(g.kind));
  if (!g.text.empty())
  {
    const Color tc = g.kind == 1 ? Color{1, 1, 1, 1} : Color{0.93f, 0.91f, 0.88f, 1};
    m_r.Text(m_r.ui, g.text, x + w / 2, y + (h - fs * 1.48f) / 2, fs, tc, Align::Center);
  }
  return w;
}

// Pistas de controles alineadas a la derecha: [icono] Texto   [icono] Texto
void App::RenderHints(const std::vector<std::pair<PsButton, std::string>>& hints, float right_x, float y)
{
  const float h = IS(36), fs = IS(25), gap = IS(30);
  const PromptStyle style = CurrentStyle();
  float x = right_x;
  for (auto it = hints.rbegin(); it != hints.rend(); ++it)
  {
    x -= m_r.TextWidth(m_r.ui, it->second, fs);
    m_r.Text(m_r.ui, it->second, x, y + (h - fs * 1.48f) / 2, fs, kGrey);
    x -= DrawGlyph(it->first, 0, 0, h, style, false) + IS(10);
    DrawGlyph(it->first, x, y, h, style);
    x -= gap;
  }
}

void App::DrawMenuButton(const Rect& slot, const std::string& label, const Texture* icon, float sel, float font,
                         bool own_background, float alpha, float dx)
{
  if (alpha <= 0.001f)
    return;
  Rect r = IR(slot);
  r.x += dx;
  if (own_background)
    m_r.Sprite(m_btn_normal, r.x, r.y, r.w, r.h, {1, 1, 1, alpha});
  if (sel > 0.01f)
  {
    // La pincelada roja de la ilustración, con un leve pulso.
    const float pulse = 1.0f + 0.012f * std::sin(m_time * 3.0f) * sel;
    const float w = r.w * 1.03f * pulse, h = r.h * 1.05f * pulse;
    m_r.Sprite(m_btn_sel, r.x + r.w / 2 - w / 2, r.y + r.h / 2 - h / 2, w, h, {1, 1, 1, sel * alpha});
  }
  const float is = IS(62);
  if (icon)
    m_r.Sprite(*icon, r.x + IS(46), r.y + r.h / 2 - is / 2, is, is,
               sel > 0.5f ? Color{1, 1, 1, alpha} : Color{0.8f, 0.78f, 0.76f, 0.95f * alpha});
  const float fs = IS(font);
  const Color tc = {0.78f + 0.2f * sel, 0.76f + 0.2f * sel, 0.74f + 0.2f * sel, alpha};
  // La etiqueta nunca pasa del borde derecho del botón (ni de la flecha de selección).
  FitText(m_r.brush, label, r.x + IS(130) + IS(6) * sel, r.y + r.h / 2 - fs * 0.74f, r.w - IS(130) - IS(80), fs, tc,
          Align::Left, false, -1);
  if (sel > 0.5f)
    m_r.DrawIcon(Icon::Chevron, r.x + r.w - IS(62), r.y + r.h / 2 - IS(13), IS(26), {1, 0.95f, 0.9f, sel * alpha});
}

// ------------------------------------------------------------------------------------------------
// Pantallas
// ------------------------------------------------------------------------------------------------

void App::RenderTitleBar()
{
  if (SDL_GetWindowFlags(m_window) & SDL_WINDOW_FULLSCREEN)
  {
    m_tb_min = m_tb_max = m_tb_close = {};
    return;
  }
  const float h = IS(kTitleH);
  m_r.Rect(0, 0, static_cast<float>(m_w), h, UiMode::Solid, {0.03f, 0.025f, 0.025f, 1.0f});
  m_r.Rect(0, h - 1, static_cast<float>(m_w), 1, UiMode::Solid, {0.3f, 0.05f, 0.04f, 0.6f});
  m_r.Text(m_r.kanji, "忍", IS(12), IS(2), IS(28), kRed);
  m_r.Text(m_r.ui, "Tenshu 2  -  Launcher", IS(62), IS(4), IS(22), {0.82f, 0.8f, 0.78f, 0.92f});

  const float bw = IS(58);
  m_tb_close = {m_w - bw, 0, bw, h};
  m_tb_max = {m_w - bw * 2, 0, bw, h};
  m_tb_min = {m_w - bw * 3, 0, bw, h};
  const Rect* btns[3] = {&m_tb_min, &m_tb_max, &m_tb_close};
  const Icon icons[3] = {Icon::Minimize, Icon::Maximize, Icon::Close};
  for (int i = 0; i < 3; i++)
  {
    const Rect& b = *btns[i];
    if (b.Contains(m_mouse_px, m_mouse_py))
      m_r.Rect(b.x, b.y, b.w, b.h, UiMode::Solid, i == 2 ? Color{0.75f, 0.06f, 0.05f, 0.95f} : Color{1, 1, 1, 0.1f});
    const float is = IS(14);
    m_r.DrawIcon(icons[i], b.x + (b.w - is) / 2, b.y + (b.h - is) / 2, is, {0.88f, 0.86f, 0.83f, 1});
  }
  Hot(m_tb_close, 5, 0, [this](float, float) { RequestQuit(); });
  Hot(m_tb_min, 5, 1, [this](float, float) { SDL_MinimizeWindow(m_window); });
  Hot(m_tb_max, 5, 2, [this](float, float) {
    if (SDL_GetWindowFlags(m_window) & SDL_WINDOW_MAXIMIZED)
      SDL_RestoreWindow(m_window);
    else
      SDL_MaximizeWindow(m_window);
  });
}

// Capa de transición: kanji en un ensō y barrido de tinta (apertura y cierre).
void App::RenderTransition()
{
  float wipe = 1.0f; // 1 = nada tapado, 0 = todo negro
  float kanji_a = 0, kanji_s = 1;
  if (IntroActive())
  {
    wipe = EaseInOut((m_intro_t - 0.75f) / 0.75f);
    kanji_a = Smooth(0.05f, 0.4f, m_intro_t) * (1.0f - Smooth(0.85f, 1.25f, m_intro_t));
    kanji_s = 1.25f - 0.25f * EaseOut(m_intro_t / 0.6f);
  }
  else if (m_outro_t >= 0)
  {
    wipe = 1.0f - EaseInOut((m_outro_t - 0.2f) / 0.55f);
    kanji_a = Smooth(0.55f, 0.8f, m_outro_t) * (1.0f - Smooth(1.0f, 1.2f, m_outro_t));
    kanji_s = 1.0f + 0.15f * Smooth(0.55f, 1.2f, m_outro_t);
  }
  if (wipe < 0.999f)
    m_r.Rect(0, 0, static_cast<float>(m_w), static_cast<float>(m_h), UiMode::InkWipe, {0, 0, 0, 1}, {}, wipe);
  if (kanji_a > 0.001f)
  {
    const float cx = m_w / 2.0f, cy = m_h / 2.0f;
    const float size = std::min(m_w, m_h) * 0.28f * kanji_s;
    m_r.Rect(cx - size * 0.95f, cy - size * 0.95f, size * 1.9f, size * 1.9f, UiMode::Enso,
             {0.7f, 0.05f, 0.04f, 0.85f * kanji_a});
    m_r.TextCentered(m_r.kanji, "忍", cx, cy, size * 1.45f, {0.9f, 0.12f, 0.08f, kanji_a});
  }
}

void App::RenderMenu()
{
  // Animación de apertura/cierre: factores de aparición de cada elemento.
  const float T = m_intro_t, O = m_outro_t;
  const float out_fade = O >= 0 ? 1.0f - Smooth(0.05f, 0.4f, O) : 1.0f;
  const float logo_a = Smooth(1.15f, 1.6f, T) * out_fade;
  const float logo_s = 1.0f + 0.05f * (1.0f - EaseOut((T - 1.15f) / 0.6f));
  const float tag_a = Smooth(1.8f, 2.2f, T) * out_fade;
  const float info_a = Smooth(2.0f, 2.4f, T) * out_fade;

  // Logo y lema de la ilustración, como capas independientes del fondo.
  {
    const float w = IS(680) * logo_s, h = IS(308) * logo_s;
    m_r.Sprite(m_logo, IX(50 + 340) - w / 2, IY(64 + 154) - h / 2, w, h, {1, 1, 1, logo_a});
  }
  m_r.Sprite(m_tagline, IX(60), IY(860), IS(420), IS(152), {1, 1, 1, tag_a});

  const char* labels[kMenuItems] = {"JUGAR", "LOGROS", "OPCIONES", "CRÉDITOS", "SALIR"};
  const Texture* icons[kMenuItems] = {&m_ic_play, &m_ic_ach, &m_ic_opt, nullptr, &m_ic_quit};
  for (int i = 0; i < kMenuItems; i++)
  {
    float e = EaseOut((T - 1.35f - i * 0.09f) / 0.38f);
    if (O >= 0)
      e *= 1.0f - EaseInOut((O - i * 0.05f) / 0.3f);
    const float sel = m_modal == Modal::None ? m_sel_anim[i] : 0.0f;
    DrawMenuButton(kMenuSlots[i], labels[i], icons[i], sel, 44, true, e, -(1.0f - e) * IS(420));
    if (!icons[i] && e > 0.001f)
    {
      // CRÉDITOS: pergamino dibujado en el hueco del icono.
      const Rect r = IR(kMenuSlots[i]);
      const float is = IS(50);
      m_r.DrawIcon(Icon::Scroll, r.x - (1.0f - e) * IS(420) + IS(52), r.y + r.h / 2 - is / 2, is,
                   sel > 0.5f ? Color{1, 1, 1, e} : Color{0.8f, 0.78f, 0.76f, 0.95f * e});
    }
    Hot(IR(kMenuSlots[i]), 0, i, [this, i](float, float) {
      m_sel = i;
      ActivateMenu(i);
    });
  }

  // Progreso de logros dentro del botón LOGROS.
  if (m_ach.GetState() == Achievements::State::Ready)
  {
    const std::string p = std::to_string(m_ach.EarnedCount()) + "/" + std::to_string(m_ach.List().size());
    const Rect r = IR(kMenuSlots[1]);
    m_r.Text(m_r.ui, p, r.x + r.w - IS(90), r.y + r.h / 2 - IS(17), IS(24), WithAlpha(kGold, info_a), Align::Right);
  }

  // Estado del disco y pistas de controles (abajo a la derecha).
  std::string status;
  Color sc = {0.82f, 0.8f, 0.78f, 0.75f};
  if (m_cfg.game_path.empty() || !Paths::FileExists(m_cfg.game_path))
  {
    status = "Sin disco: copia tu imagen en la carpeta game/ o arrástrala aquí";
    sc = {1.0f, 0.75f, 0.3f, 0.95f};
  }
  else
  {
    status = "Disco: " + BaseName(m_cfg.game_path);
  }
  m_r.Text(m_r.ui, Truncate(m_r.ui, status, IS(20), m_w * 0.5f), m_w - IS(28), m_h - IS(92), IS(20),
           WithAlpha(sc, info_a), Align::Right);
  if (info_a > 0.99f)
    RenderHints({{PsButton::Accept, "Aceptar"}}, m_w - IS(28), m_h - IS(58));
}

void App::RenderPlayChoice()
{
  DimScreen(0.55f);
  const float x = 470, y = 300, w = 620, h = 470;
  Panel(x, y, w, h);
  m_r.Text(m_r.brush, "JUGAR", IX(x + 44), IY(y + 24), IS(52), kWhite);
  const char* labels[3] = {"CONTINUAR", "NUEVA PARTIDA", "CANCELAR"};
  const Texture* icons[3] = {&m_ic_play, &m_ic_ach, &m_ic_quit};
  for (int i = 0; i < 3; i++)
  {
    const Rect slot = {x + 70, y + 120 + i * 96.0f, 462, 88};
    DrawMenuButton(slot, labels[i], icons[i], m_sel_anim[i], 34, true);
    Hot(IR(slot), 3, i, [this, i](float, float) {
      m_choice_sel = i;
      Accept();
    });
  }
  RenderHints({{PsButton::Accept, "Aceptar"}, {PsButton::Back, "Volver"}}, IX(x + w - 30), IY(y + h + 16));
}

void App::RenderOptionCategories()
{
  const float px = 40, py = 56, pw = 640, ph = 950;
  Panel(px, py, pw, ph);
  m_r.Text(m_r.brush, "OPCIONES", IX(px + 44), IY(py + 20), IS(56), kWhite);
  m_r.Sprite(m_ic_opt, IX(px + pw - 110), IY(py + 26), IS(62), IS(62), {0.8f, 0.78f, 0.76f, 0.8f});

  for (int i = 0; i <= kOptCategoryCount; i++)
  {
    const bool back = (i == kOptCategoryCount);
    const Rect slot = {px + 60, py + 140 + i * 104.0f + (back ? 30.0f : 0.0f), 462, 88};
    DrawMenuButton(slot, back ? "VOLVER" : kOptCategories[i], back ? &m_ic_quit : nullptr, m_sel_anim[i], 36, true);
    if (!back)
    {
      // Kanji de la categoría en el lugar del icono.
      const Rect r = IR(slot);
      const bool sel = m_sel_anim[i] > 0.5f;
      m_r.TextCentered(m_r.kanji, kOptKanji[i], r.x + IS(77), r.y + r.h / 2, IS(64),
                       sel ? Color{1, 0.96f, 0.92f, 1} : Color{0.8f, 0.76f, 0.72f, 0.95f});
    }
    Hot(IR(slot), 1, i, [this, i](float, float) {
      m_opt_sel = i;
      Accept();
    });
  }

  // Descripción de la categoría seleccionada.
  if (m_opt_sel < kOptCategoryCount)
  {
    float ly = py + 140 + (kOptCategoryCount + 1) * 104.0f + 60;
    for (const std::string& line : Wrap(m_r.ui, kOptCategoryHelp[m_opt_sel], IS(25), IS(pw - 100)))
    {
      m_r.Text(m_r.ui, line, IX(px + 60), IY(ly), IS(25), {0.84f, 0.81f, 0.78f, 0.95f});
      ly += 34;
    }
  }

  // Créditos y licencias.
  const float cx = m_w - IS(28);
  const Color cc = {0.8f, 0.78f, 0.76f, 0.8f};
  m_r.Text(m_r.ui, std::string("Kage no Mon ") + kVersion + " · software libre (GPL-3.0) · sin fines de lucro",
           cx, m_h - IS(150), IS(19), cc, Align::Right);
  m_r.Text(m_r.ui, "Núcleo: SwanStation (GPL-3.0) · Logros: rcheevos (MIT) / RetroAchievements", cx, m_h - IS(124),
           IS(19), cc, Align::Right);
  m_r.Text(m_r.ui, "Fuentes: Permanent Marker (Apache-2.0), Oswald, Shojumaru, Yuji Syuku (OFL)", cx, m_h - IS(98),
           IS(19), cc, Align::Right);
  RenderHints({{PsButton::Accept, "Aceptar"}, {PsButton::Back, "Volver"}}, m_w - IS(28), m_h - IS(58));
}

void App::RenderOptions()
{
  DimScreen(0.45f);
  if (m_opt_page < 0)
  {
    RenderOptionCategories();
    return;
  }

  const float px = 40, py = 56, pw = 880, ph = 950;
  Panel(px, py, pw, ph);
  m_r.Text(m_r.ui, "OPCIONES", IX(px + 48), IY(py + 18), IS(22), kDim);
  m_r.Text(m_r.brush, kOptCategories[m_opt_page], IX(px + 44), IY(py + 42), IS(52), kWhite);
  m_r.TextCentered(m_r.kanji, kOptKanji[m_opt_page], IX(px + pw - 80), IY(py + 70), IS(84),
                   {0.6f, 0.04f, 0.04f, 0.45f});

  auto yesno = [](bool b) { return std::string(b ? "Sí" : "No"); };
  const bool hw = m_cfg.hardware_renderer;

  struct Row
  {
    std::string label, value;
    bool enabled = true, arrows = false;
    float bar = -1; // 0..1: la fila se dibuja como barra deslizante
  };
  auto info = [&](int id) -> Row {
    switch (id)
    {
      case OPT_GAME:
        return {"Imagen del juego", m_cfg.game_path.empty() ? "(sin seleccionar)" : BaseName(m_cfg.game_path)};
      case OPT_BIOS:
      {
        const bool has_files = !Paths::FindBiosFiles().empty();
        const bool chosen = !m_cfg.bios_file.empty() && Paths::FileExists(Paths::BiosDir() + "/" + m_cfg.bios_file);
        return {"BIOS", chosen ? BiosLabel(m_cfg.bios_file) : "OpenBIOS (libre, integrada)", true, has_files};
      }
      case OPT_BIOS_LOAD: return {"Cargar BIOS nueva…", ""};
      case OPT_FASTBOOT: return {"Arranque rápido", yesno(m_cfg.fast_boot), true, true};
      case OPT_HARDCORE:
        return {"Modo hardcore",
                m_cfg.hardcore ? (HasSonyBios() ? "Sí" : "Sí (falta la BIOS original)") : "No", true, true};
      case OPT_RENDERER: return {"Renderizador", hw ? "OpenGL (mejorado)" : "Software (preciso)", true, true};
      case OPT_SCALE: return {"Resolución interna", std::to_string(m_cfg.resolution_scale) + "x", hw, true};
      case OPT_TEXFILTER:
      {
        const std::string tf = m_cfg.texture_filter == "Nearest"    ? "Original"
                               : m_cfg.texture_filter == "Bilinear" ? "Bilineal"
                                                                     : m_cfg.texture_filter + " (HD)";
        return {"Filtro de texturas", tf, hw, true};
      }
      case OPT_PGXP:
        return {"Corregir geometría (PGXP)", !m_cfg.pgxp ? "No" : m_cfg.pgxp_cpu ? "Máxima" : "Sí", hw, true};
      case OPT_WIDESCREEN:
        return {"Aspecto de la pantalla",
                m_ws_available ? (m_cfg.widescreen ? "16:9" : "4:3") : "4:3 (16:9 no disponible para este disco)",
                m_ws_available, m_ws_available};
      case OPT_SIDEART: return {"Bordes laterales", m_cfg.side_art ? "Ilustración" : "Negro", true, true};
      case OPT_SMOOTH: return {"Suavizar imagen", yesno(m_cfg.smooth_filter), true, true};
      case OPT_FULLSCREEN: return {"Pantalla completa", yesno(m_cfg.fullscreen), true, true};
      case OPT_VOLUME: return {"Volumen del juego", std::to_string(m_cfg.volume) + "%", true, true};
      case OPT_MUSIC: return {"Volumen de la música", std::to_string(m_cfg.music_volume) + "%", true, true};
      case OPT_SFX: return {"Volumen de efectos", std::to_string(m_cfg.sfx_volume) + "%", true, true};
      case OPT_PROMPTS:
      {
        const std::string p = m_cfg.prompt_style == "keyboard"      ? "Teclado"
                              : m_cfg.prompt_style == "xbox"        ? "Xbox"
                              : m_cfg.prompt_style == "playstation" ? "PlayStation"
                                                                    : "Automático";
        return {"Iconos de botones", p, true, true};
      }
      case OPT_FREE_CAMERA:
      {
        FreeCamera probe;
        const bool known = probe.SetGame(Paths::ReadDiscSerial(m_cfg.game_path));
        return {"Cámara libre (joystick derecho)",
                !known ? "No disponible para este disco" : m_cfg.hardcore ? "No (modo hardcore)" : yesno(m_cfg.free_camera),
                known && !m_cfg.hardcore, known && !m_cfg.hardcore};
      }
      case OPT_MOVEMENT:
      {
        FreeCamera probe;
        const bool ok = probe.SetGame(Paths::ReadDiscSerial(m_cfg.game_path)) && !m_cfg.hardcore && m_cfg.free_camera;
        return {"Movimiento", !ok ? "Clásico (tanque)" : m_cfg.modern_controls ? "360° (relativo a la cámara)" : "Clásico (tanque)",
                ok, ok};
      }
      case OPT_CAMERA_SPEED:
      {
        FreeCamera probe;
        const bool on = probe.SetGame(Paths::ReadDiscSerial(m_cfg.game_path)) && !m_cfg.hardcore && m_cfg.free_camera;
        return {"Velocidad de la cámara", std::to_string(m_cfg.camera_speed) + "%", on, on,
                (m_cfg.camera_speed - 20) / 280.0f};
      }
      case OPT_GAME_PROMPTS:
      {
        return {"Iconos dentro del juego", m_cfg.game_prompts ? "Según el mando" : "Originales (PlayStation)", true,
                true};
      }
      case OPT_REMAP_KB:
        return {"Personalizar teclado", m_cfg.kb_mouse ? "Esquema fijo (teclado y ratón)" : "", !m_cfg.kb_mouse, false};
      case OPT_KB_SCHEME:
        return {"Esquema de teclado", m_cfg.kb_mouse ? "Teclado y ratón" : "Sólo teclado", true, true};
      case OPT_REMAP_PAD: return {"Personalizar mando", ""};
      case OPT_RESET_CONTROLS:
        return {"Restaurar controles por defecto", m_reset_confirm > 0 ? "¿Seguro? Pulsa otra vez" : ""};
      case OPT_CONTROLS: return {"Ver esquema de controles", ""};
      case OPT_BACK: return {"Volver", ""};
    }
    return {};
  };

  const std::vector<int>& rows = OptPageRows(m_opt_page);
  m_opt_sel = std::clamp(m_opt_sel, 0, static_cast<int>(rows.size()) - 1);
  const float row0 = py + 150, rh = 62;
  const float label_x = px + 52;
  const float vx0 = px + 430, vx1 = px + pw - 64; // columna de valores (flechas en sus extremos)
  for (size_t i = 0; i < rows.size(); i++)
  {
    const int id = rows[i];
    const Row r = info(id);
    const float y = row0 + i * rh + (id == OPT_BACK ? 24.0f : 0.0f);
    const bool sel = static_cast<int>(i) == m_opt_sel;
    const Rect rr = IR({px + 22, y, pw - 44, rh - 6});
    if (sel)
      m_r.Sprite(m_btn_sel, rr.x - IS(10), rr.y - IS(7), rr.w + IS(20), rr.h + IS(14), {1, 1, 1, 0.95f});
    const float a = r.enabled ? 1.0f : 0.4f;
    const Color lc = sel ? Color{1, 0.98f, 0.96f, a} : Color{0.86f, 0.84f, 0.82f, a};
    const float ty = y + 10;
    const bool has_value = !r.value.empty();
    // La etiqueta ocupa todo el ancho si la fila no tiene valor.
    FitText(m_r.ui, r.label, IX(label_x), IY(ty), IS((has_value ? vx0 - 30 : vx1 + 20) - label_x), IS(30), lc,
            Align::Left, sel, m_opt_page * 100 + static_cast<int>(i) * 2);
    if (has_value)
    {
      const Color vc = WithAlpha(lc, sel ? 1.0f : 0.88f);
      if (r.bar >= 0)
      {
        // Barra deslizante: flechas, pista con la parte llena y el valor a la derecha.
        m_r.DrawIcon(Icon::ArrowLeft, IX(vx0), IY(y + 16), IS(22), WithAlpha(lc, sel ? 1.0f : 0.5f));
        m_r.DrawIcon(Icon::ArrowRight, IX(vx1), IY(y + 16), IS(22), WithAlpha(lc, sel ? 1.0f : 0.5f));
        const float bx0 = vx0 + 44, bx1 = vx1 - 96, by = y + 24, bh = 8;
        const float fill = std::clamp(r.bar, 0.0f, 1.0f);
        m_r.Rect(IX(bx0), IY(by), IS(bx1 - bx0), IS(bh), UiMode::Solid, {1, 1, 1, 0.12f * a});
        m_r.Rect(IX(bx0), IY(by), IS((bx1 - bx0) * fill), IS(bh), UiMode::Solid,
                 sel ? Color{1.0f, 0.82f, 0.45f, a} : WithAlpha(kGold, 0.75f * a));
        m_r.Rect(IX(bx0 + (bx1 - bx0) * fill - 3), IY(by - 6), IS(6), IS(bh + 12), UiMode::Solid, WithAlpha(lc, a));
        m_r.Text(m_r.ui, r.value, IX(vx1 - 22), IY(ty + 2), IS(27), vc, Align::Right);
      }
      else if (r.arrows)
      {
        m_r.DrawIcon(Icon::ArrowLeft, IX(vx0), IY(y + 16), IS(22), WithAlpha(lc, sel ? 1.0f : 0.5f));
        m_r.DrawIcon(Icon::ArrowRight, IX(vx1), IY(y + 16), IS(22), WithAlpha(lc, sel ? 1.0f : 0.5f));
        FitText(m_r.ui, r.value, IX(vx0 + 40), IY(ty + 2), IS(vx1 - vx0 - 58), IS(27), vc, Align::Center, sel,
                m_opt_page * 100 + static_cast<int>(i) * 2 + 1);
      }
      else
      {
        FitText(m_r.ui, r.value, IX(vx0 - 20), IY(ty + 2), IS(vx1 + 6 - vx0), IS(27), vc, Align::Right, sel,
                m_opt_page * 100 + static_cast<int>(i) * 2 + 1);
      }
    }
    const int index = static_cast<int>(i);
    const bool bar = r.bar >= 0 && r.enabled;
    Hot(rr, 1, index, [this, index, id, vx0, vx1, bar](float mx, float) {
      m_opt_sel = index;
      const float bx0 = IX(vx0 + 44), bx1 = IX(vx1 - 96);
      if (bar && id == OPT_CAMERA_SPEED && mx >= bx0 - 8 && mx <= bx1 + 8)
      {
        // Clic en la barra: salta a ese valor (en pasos de 10).
        const float f = std::clamp((mx - bx0) / (bx1 - bx0), 0.0f, 1.0f);
        m_cfg.camera_speed = std::clamp(static_cast<int>(std::lround((20 + f * 280) / 10.0f)) * 10, 20, 300);
        m_audio.Play(Sfx::Move);
        m_cfg.Save();
        return;
      }
      if (mx > IX(vx0 - 15) && mx < IX(vx0 + 40))
        OptionChange(id, -1);
      else if (mx > IX(vx1 - 15))
        OptionChange(id, 1);
      else
        OptionChange(id, 0);
    });
  }

  // Ayuda de la fila seleccionada.
  const char* help = "";
  switch (rows[m_opt_sel])
  {
    case OPT_GAME: help = "Tu copia del disco (.cue, .bin, .chd…). La carpeta game/ del launcher se detecta sola."; break;
    case OPT_BIOS:
      help = "OpenBIOS es libre y viene integrada. Si un juego da problemas, carga la BIOS de tu consola y elígela "
             "aquí.";
      break;
    case OPT_BIOS_LOAD: help = "Copia una BIOS (archivo de 512 KB) a la carpeta bios/ y la deja elegida."; break;
    case OPT_FASTBOOT: help = "Salta la animación de arranque de la consola."; break;
    case OPT_HARDCORE:
      help = "Como en la consola: BIOS original, sin estados guardados ni trucos; sólo la tarjeta de memoria. Los "
             "logros conseguidos así se marcan como hardcore.";
      break;
    case OPT_RENDERER: help = "OpenGL permite alta resolución. Software es el más fiel al hardware original."; break;
    case OPT_SCALE:
      help = "Resolución de PlayStation multiplicada: 4x ≈ 960p, 5x ≈ 1080p. 3x no se ofrece porque corrompe el texto "
             "del juego.";
      break;
    case OPT_TEXFILTER: help = "xBR y JINC2 redibujan las texturas en alta definición. Original = píxeles tal cual."; break;
    case OPT_PGXP:
      help = "Evita que polígonos y texturas tiemblen o «respiren». Máxima corrige también el suelo y el "
             "escenario (usa algo más de CPU).";
      break;
    case OPT_WIDESCREEN:
      help = "4:3 es el formato original. 16:9 usa un parche del propio juego (El_Patas y PeterDelta): más campo de "
             "visión; el HUD queda en su sitio, aunque algo más ancho.";
      break;
    case OPT_SIDEART: help = "Con el juego en 4:3, los lados muestran la ilustración del launcher oscurecida o negro."; break;
    case OPT_SMOOTH: help = "Suaviza la imagen al escalarla a la ventana (No = píxeles nítidos)."; break;
    case OPT_FULLSCREEN: help = "También con F11 o Alt+Enter."; break;
    case OPT_VOLUME: help = "Volumen del sonido del juego."; break;
    case OPT_MUSIC: help = "Música del launcher (menús y pausa)."; break;
    case OPT_SFX: help = "Sonidos del launcher al moverte, elegir y cancelar."; break;
    case OPT_PROMPTS: help = "Iconos de botones del launcher. Automático cambia según uses teclado o mando."; break;
    case OPT_MOVEMENT:
      help = "360°: corre hacia donde empujas el joystick, según la cámara. Clásico: controles de tanque.";
      break;
    case OPT_CAMERA_SPEED:
      help = "Lo rápido que gira la cámara libre con el joystick derecho. 100% es la velocidad normal; también "
             "puedes hacer clic en la barra.";
      break;
    case OPT_FREE_CAMERA:
      help = "Joystick derecho: gira la cámara (respeta los muros). R3: la devuelve detrás. L3: fija al enemigo "
             "a la vista más cercano (joystick derecho a un lado cambia de enemigo). No en hardcore.";
      break;
    case OPT_GAME_PROMPTS:
      help = "Los iconos de botones de los menús del juego se cambian por los de tu mando de Xbox o tu teclado, "
             "con tus asignaciones.";
      break;
    case OPT_CONTROLS: help = "Teclado, mando de Xbox y mando de PlayStation, con tus asignaciones."; break;
    case OPT_REMAP_KB: help = "Cambia qué tecla corresponde a cada botón. No afecta al mando."; break;
    case OPT_KB_SCHEME:
      help = "Teclado y ratón: WASD para moverse, el ratón mueve la cámara, clic izquierdo ataca, clic derecho usa "
             "la herramienta y la rueda la cambia. Esquema completo en Ver esquema de controles.";
      break;
    case OPT_REMAP_PAD: help = "Cambia qué botón del mando corresponde a cada botón. No afecta al teclado."; break;
    case OPT_RESET_CONTROLS: help = "Vuelve a la asignación original de teclado y mando (pide confirmación)."; break;
    default: help = ""; break;
  }
  float ly = row0 + rows.size() * rh + 60;
  for (const std::string& line : Wrap(m_r.ui, help, IS(24), IS(pw - 100)))
  {
    if (ly > py + ph - 90)
      break;
    m_r.Text(m_r.ui, line, IX(px + 52), IY(ly), IS(24), {0.84f, 0.81f, 0.78f, 0.95f});
    ly += 33;
  }
  if (m_opt_page == 1)
    m_r.Text(m_r.ui, "Los cambios de imagen se aplican la próxima vez que inicies el juego.", IX(px + 52),
             IY(py + ph - 64), IS(20), kDim);
  RenderHints({{PsButton::Accept, "Aceptar"}, {PsButton::Back, "Volver"}}, m_w - IS(28), m_h - IS(58));
}

void App::RenderRemap()
{
  DimScreen(0.5f);
  const float px = 40, py = 56, pw = 900, ph = 950;
  Panel(px, py, pw, ph);
  const bool kb = (m_remap_device == 0);
  m_r.Text(m_r.ui, "OPCIONES  ·  CONTROLES", IX(px + 48), IY(py + 18), IS(22), kDim);
  m_r.Text(m_r.brush, kb ? "TECLADO" : "MANDO", IX(px + 44), IY(py + 42), IS(52), kWhite);
  m_r.TextCentered(m_r.kanji, "操", IX(px + pw - 80), IY(py + 70), IS(84), {0.6f, 0.04f, 0.04f, 0.45f});

  // Estilo de los iconos del mando: el elegido en Opciones o el del mando conectado.
  PromptStyle style = PromptStyle::Keyboard;
  if (!kb)
    style = m_cfg.prompt_style == "playstation" ? PromptStyle::PlayStation
            : m_cfg.prompt_style == "xbox"      ? PromptStyle::Xbox
                                                : PadStyle();
  // Botón de PS1 de cada índice de asignación, para reutilizar los iconos.
  auto as_button = [](int id) -> PsButton {
    switch (id)
    {
      case RETRO_DEVICE_ID_JOYPAD_UP: return PsButton::DUp;
      case RETRO_DEVICE_ID_JOYPAD_DOWN: return PsButton::DDown;
      case RETRO_DEVICE_ID_JOYPAD_LEFT: return PsButton::DLeft;
      case RETRO_DEVICE_ID_JOYPAD_RIGHT: return PsButton::DRight;
      case RETRO_DEVICE_ID_JOYPAD_B: return PsButton::Cross;
      case RETRO_DEVICE_ID_JOYPAD_A: return PsButton::Circle;
      case RETRO_DEVICE_ID_JOYPAD_Y: return PsButton::Square;
      case RETRO_DEVICE_ID_JOYPAD_X: return PsButton::Triangle;
      case RETRO_DEVICE_ID_JOYPAD_L: return PsButton::L1;
      case RETRO_DEVICE_ID_JOYPAD_R: return PsButton::R1;
      case RETRO_DEVICE_ID_JOYPAD_L2: return PsButton::L2;
      case RETRO_DEVICE_ID_JOYPAD_R2: return PsButton::R2;
      case RETRO_DEVICE_ID_JOYPAD_L3: return PsButton::L3;
      case RETRO_DEVICE_ID_JOYPAD_R3: return PsButton::R3;
      case RETRO_DEVICE_ID_JOYPAD_START: return PsButton::Start;
      default: return PsButton::Select;
    }
  };

  const float colx[2] = {px + 30, px + 465};
  const float cw = 405, y0 = py + 140, rh = 62;
  for (int i = 0; i < kPsButtons; i++)
  {
    const int id = kRemapOrder[i];
    const float x = colx[i / 8], y = y0 + (i % 8) * rh;
    const bool sel = (i == m_remap_sel);
    const bool waiting = sel && m_remap_wait == id;
    const Rect rr = IR({x, y, cw, rh - 8});
    if (sel)
      m_r.Sprite(m_btn_sel, rr.x - IS(8), rr.y - IS(6), rr.w + IS(16), rr.h + IS(12), {1, 1, 1, 0.95f});
    const Color lc = sel ? Color{1, 0.98f, 0.96f, 1} : Color{0.86f, 0.84f, 0.82f, 1};
    m_r.Text(m_r.ui, PsButtonName(id), rr.x + IS(20), rr.y + IS(8), IS(28), lc);
    if (waiting)
    {
      const float pulse = 0.6f + 0.4f * std::sin(m_time * 8.0f);
      const std::string msg = std::string(kb ? "Pulsa una tecla" : "Pulsa un botón") + "… (" +
                              std::to_string(static_cast<int>(std::ceil(m_remap_timer))) + ")";
      m_r.Text(m_r.ui, msg, rr.x + rr.w - IS(20), rr.y + IS(10), IS(24), WithAlpha(kGold, pulse), Align::Right);
    }
    else
    {
      const float gh = IS(38);
      const float gw = DrawGlyph(as_button(id), 0, 0, gh, style, false);
      const float max_w = rr.w - IS(170);
      if (gw <= max_w)
        DrawGlyph(as_button(id), rr.x + rr.w - IS(18) - gw, rr.y + (rr.h - gh) / 2, gh, style);
      else
        FitText(m_r.ui, kb ? KeyLabel(m_cfg.key_bind[id]) : "…", rr.x + IS(150), rr.y + IS(10), max_w, IS(24),
                lc, Align::Right, sel, 20000 + i);
    }
    Hot(rr, 7, i, [this, i](float, float) {
      m_remap_sel = i;
      Accept();
    });
  }

  const char* extra[2] = {"Restaurar por defecto", "Volver"};
  for (int k = 0; k < 2; k++)
  {
    const int i = kPsButtons + k;
    const float y = y0 + 8 * rh + 22 + k * rh;
    const bool sel = (i == m_remap_sel);
    const Rect rr = IR({px + 30, y, pw - 60, rh - 8});
    if (sel)
      m_r.Sprite(m_btn_sel, rr.x - IS(8), rr.y - IS(6), rr.w + IS(16), rr.h + IS(12), {1, 1, 1, 0.95f});
    m_r.Text(m_r.ui, extra[k], rr.x + IS(20), rr.y + IS(8), IS(28),
             sel ? Color{1, 0.98f, 0.96f, 1} : Color{0.86f, 0.84f, 0.82f, 1});
    Hot(rr, 7, i, [this, i](float, float) {
      m_remap_sel = i;
      Accept();
    });
  }

  std::string help;
  if (m_remap_wait >= 0)
    help = kb ? "Pulsa la tecla nueva. Esc cancela. Las teclas F y Esc están reservadas."
              : "Pulsa el botón nuevo en el mando. Esc o un clic cancelan.";
  else
    help = "Elige un botón y pulsa Aceptar para cambiarlo. Si la tecla ya la usa otro botón, se intercambian. "
           "Los menús del launcher siempre se manejan con las flechas, Enter y Esc (o cruceta, A y B).";
  if (!kb && !m_pad)
    help += "\nNo hay ningún mando conectado.";
  float ly = y0 + 10 * rh + 50;
  for (const std::string& line : Wrap(m_r.ui, help, IS(23), IS(pw - 90)))
  {
    m_r.Text(m_r.ui, line, IX(px + 48), IY(ly), IS(23), {0.84f, 0.81f, 0.78f, 0.95f});
    ly += 31;
  }
  RenderHints({{PsButton::Accept, "Cambiar"}, {PsButton::Back, "Volver"}}, m_w - IS(28), m_h - IS(58));
}

void App::RenderCheats()
{
  DimScreen(0.5f);
  const float px = 40, py = 56, pw = 900, ph = 950;
  Panel(px, py, pw, ph);
  m_r.Text(m_r.ui, "OPCIONES", IX(px + 48), IY(py + 18), IS(22), kDim);
  m_r.Text(m_r.brush, "TRUCOS", IX(px + 44), IY(py + 42), IS(52), kWhite);
  m_r.TextCentered(m_r.kanji, "術", IX(px + pw - 80), IY(py + 70), IS(84), {0.6f, 0.04f, 0.04f, 0.45f});

  const int n = static_cast<int>(m_cheat_list.size());
  int active = 0;
  for (const Cheat& c : m_cheat_list)
    active += CheatEnabled(c.name);
  const std::string state = n == 0 ? "" : active == 0 ? "Ningún truco activo"
                                                       : std::to_string(active) + (active == 1 ? " truco activo"
                                                                                               : " trucos activos");
  m_r.Text(m_r.ui, state, IX(px + pw - 40), IY(py + 118), IS(22), active ? kGold : kDim, Align::Right);
  m_r.Text(m_r.ui, active ? "Los logros se pausarán en la próxima partida." : "Se aplican al iniciar la partida.",
           IX(px + 48), IY(py + 118), IS(22), active ? Color{1.0f, 0.75f, 0.3f, 0.95f} : kDim);

  const float top = py + 160, view_h = 560, rh = 56;
  if (n == 0)
  {
    for (const std::string& line : Wrap(m_r.ui,
                                        "No hay trucos disponibles para este disco. Hay trucos para las versiones de "
                                        "Tenchu 2 de España, Europa, Alemania, EE. UU. y Japón.",
                                        IS(26), IS(pw - 100)))
    {
      m_r.Text(m_r.ui, line, IX(px + 52), IY(top), IS(26), kGrey);
    }
  }
  m_r.SetClip(IX(px + 10), IY(top), IS(pw - 20), IS(view_h));
  for (int i = 0; i < n; i++)
  {
    const float y = top + i * rh - m_cheat_scroll;
    if (y + rh < top || y > top + view_h)
      continue;
    const Cheat& c = m_cheat_list[i];
    const bool sel = (i == m_cheat_sel), on = CheatEnabled(c.name);
    const Rect rr = IR({px + 22, y, pw - 44, rh - 6});
    if (sel)
      m_r.Sprite(m_btn_sel, rr.x - IS(10), rr.y - IS(7), rr.w + IS(20), rr.h + IS(14), {1, 1, 1, 0.95f});
    const Color lc = sel ? Color{1, 0.98f, 0.96f, 1} : on ? kWhite : Color{0.8f, 0.78f, 0.76f, 1};
    FitText(m_r.ui, c.label, rr.x + IS(30), rr.y + IS(8), rr.w - IS(200), IS(27), lc, Align::Left, sel, 30000 + i);
    m_r.Text(m_r.ui, on ? "Activado" : "No", rr.x + rr.w - IS(26), rr.y + IS(9), IS(25),
             on ? kGold : Color{0.6f, 0.58f, 0.56f, 1}, Align::Right);
    Hot(rr, 8, i, [this, i](float, float) {
      m_cheat_sel = i;
      Accept();
    });
  }
  m_r.ClearClip();

  const char* extra[2] = {"Desactivar todos", "Volver"};
  for (int k = 0; k < 2; k++)
  {
    const int i = n + k;
    const float y = top + view_h + 20 + k * rh;
    const bool sel = (i == m_cheat_sel);
    const Rect rr = IR({px + 22, y, pw - 44, rh - 6});
    if (sel)
      m_r.Sprite(m_btn_sel, rr.x - IS(10), rr.y - IS(7), rr.w + IS(20), rr.h + IS(14), {1, 1, 1, 0.95f});
    m_r.Text(m_r.ui, extra[k], rr.x + IS(30), rr.y + IS(8), IS(27),
             sel ? Color{1, 0.98f, 0.96f, 1} : Color{0.86f, 0.84f, 0.82f, 1});
    Hot(rr, 8, i, [this, i](float, float) {
      m_cheat_sel = i;
      Accept();
    });
  }
  m_r.Text(m_r.ui, "Trucos de la base de datos comunitaria de DuckStation (chtdb).", IX(px + 48), IY(py + ph - 60),
           IS(19), kDim);
  RenderHints({{PsButton::Accept, "Activar / desactivar"}, {PsButton::Back, "Volver"}}, m_w - IS(28), m_h - IS(58));
}

void App::RenderCredits()
{
  DimScreen(0.55f);
  const float px = 40, py = 56, pw = 1100, ph = 950;
  Panel(px, py, pw, ph);
  m_r.Text(m_r.brush, "CRÉDITOS", IX(px + 44), IY(py + 20), IS(56), kWhite);
  m_r.TextCentered(m_r.kanji, "忍", IX(px + pw - 80), IY(py + 66), IS(84), {0.6f, 0.04f, 0.04f, 0.45f});

  struct Line
  {
    int kind; // 0 sección, 1 nombre + función, 2 título, 3 nota
    std::string a, b;
  };
  std::vector<Line> lines = {
    {2, "KAGE NO MON", ""},
    {3, std::string("La Puerta de las Sombras  ·  ") + kVersion, ""},
    {0, "PROYECTO", ""},
    {1, "daetrox", "Idea, dirección, diseño e ilustración del launcher"},
    {1, "Claude (Anthropic)", "Programación, en colaboración con daetrox"},
    {1, "Composición original", "Música y efectos de sonido (koto, shakuhachi y taiko)"},
    {0, "EMULACIÓN", ""},
    {1, "SwanStation", "Núcleo de PlayStation para libretro, derivado de DuckStation  ·  GPL-3.0"},
    {1, "Connor McLaughlin (stenzek) y colaboradores", "Autores de DuckStation, base de SwanStation"},
    {1, "OpenBIOS", "BIOS libre de PCSX-Redux  ·  autores de PCSX-Redux  ·  MIT"},
    {1, "libretro", "API de núcleos de emulación  ·  equipo de libretro"},
    {0, "LOGROS", ""},
    {1, "RetroAchievements.org", "Plataforma y base de datos de logros"},
    {1, "rcheevos", "Biblioteca de logros de RetroAchievements  ·  MIT"},
  };
  if (m_ach.GetState() == Achievements::State::Ready)
  {
    std::string authors;
    for (const std::string& a : m_ach.Authors())
      authors += (authors.empty() ? "" : ", ") + a;
    if (!authors.empty())
      lines.push_back({1, authors, "Autoría del set de logros de " + m_ach.GameTitle()});
  }
  const std::vector<Line> rest = {
    {0, "PANTALLA PANORÁMICA", ""},
    {1, "El_Patas y PeterDelta", "Parche 16:9 nativo  ·  base de datos chtdb de DuckStation"},
    {0, "TRUCOS", ""},
    {1, "Comunidad de chtdb (DuckStation)", "Códigos de trucos de Tenchu 2, de sus respectivos autores"},
    {0, "BIBLIOTECAS", ""},
    {1, "SDL2", "Sam Lantinga y colaboradores  ·  zlib"},
    {1, "stb_truetype, stb_image, stb_vorbis", "Sean Barrett  ·  dominio público"},
    {0, "TIPOGRAFÍAS", ""},
    {1, "Permanent Marker", "Font Diner  ·  Apache 2.0"},
    {1, "Oswald", "The Oswald Project Authors  ·  SIL OFL 1.1"},
    {1, "Shojumaru", "Astigmatic (Brian J. Bonislawsky)  ·  SIL OFL 1.1"},
    {1, "Kaushan Script", "Pablo Impallari e Igino Marini  ·  SIL OFL 1.1"},
    {1, "Yuji Syuku", "The Yuji Project Authors (Kinuta Font Factory)  ·  SIL OFL 1.1"},
    {0, "CON RESPETO A", ""},
    {1, "Acquire y Activision", "Creadores de Tenchu 2: Birth of the Stealth Assassins (2000)"},
    {3, "Tenchu es una marca registrada de sus respectivos propietarios.", ""},
    {3, "Este proyecto no está afiliado a ellos.", ""},
    {3, "Software libre (GPL-3.0)  ·  sin fines de lucro", ""},
  };
  lines.insert(lines.end(), rest.begin(), rest.end());

  // Texto centrado que sube; se recorta al interior del panel.
  const float cx = px + pw / 2, top = py + 110, bottom = py + ph - 80;
  m_r.SetClip(IX(px + 10), IY(top), IS(pw - 20), IS(bottom - top));
  float y = top + 40 - m_credits_scroll;
  for (const Line& l : lines)
  {
    switch (l.kind)
    {
      case 0:
        y += 40;
        m_r.Text(m_r.brush, l.a, IX(cx), IY(y), IS(30), kGold, Align::Center);
        m_r.Rect(IX(cx - 60), IY(y + 52), IS(120), IS(2), UiMode::Solid, {0.75f, 0.1f, 0.06f, 0.8f});
        y += 74;
        break;
      case 1:
        m_r.Text(m_r.ui, Truncate(m_r.ui, l.a, IS(29), IS(pw - 120)), IX(cx), IY(y), IS(29), kWhite, Align::Center);
        m_r.Text(m_r.ui, Truncate(m_r.ui, l.b, IS(21), IS(pw - 120)), IX(cx), IY(y + 40), IS(21), kGrey,
                 Align::Center);
        y += 86;
        break;
      case 2:
        m_r.Text(m_r.brush, l.a, IX(cx), IY(y), IS(54), kWhite, Align::Center);
        y += 80;
        break;
      default:
        m_r.Text(m_r.ui, l.a, IX(cx), IY(y), IS(21), kDim, Align::Center);
        y += 34;
        break;
    }
  }
  m_r.ClearClip();
  m_credits_height = y + m_credits_scroll - top;
  // Fundidos arriba y abajo para que el texto entre y salga suavemente.
  for (int k = 0; k < 12; k++)
  {
    const float a = 0.9f * (1.0f - k / 12.0f);
    m_r.Rect(IX(px + 10), IY(top + k * 4), IS(pw - 20), IS(4), UiMode::Solid, {0.035f, 0.025f, 0.025f, a});
    m_r.Rect(IX(px + 10), IY(bottom - (k + 1) * 4), IS(pw - 20), IS(4), UiMode::Solid,
             {0.035f, 0.025f, 0.025f, a});
  }
  RenderHints({{PsButton::Back, "Volver"}}, IX(px + pw - 30), IY(py + ph - 64));
}

void App::RenderConfirmReset()
{
  DimScreen(0.6f);
  const float x = 450, y = 300, w = 680, h = 470;
  Panel(x, y, w, h);
  m_r.Text(m_r.brush, "REINICIAR LOGROS", IX(x + 44), IY(y + 24), IS(44), kWhite);
  const int n = m_ach.EarnedCount();
  const std::string msg = (n == 1 ? std::string("Se borrará el logro desbloqueado")
                                  : "Se borrarán los " + std::to_string(n) + " logros desbloqueados") +
                          " en este equipo. No se puede deshacer.";
  float ty = y + 96;
  for (const std::string& line : Wrap(m_r.ui, msg, IS(25), IS(w - 90)))
  {
    m_r.Text(m_r.ui, line, IX(x + 46), IY(ty), IS(25), kGrey);
    ty += 34;
  }
  const char* labels[2] = {"REINICIAR", "CANCELAR"};
  const Texture* icons[2] = {&m_ic_ach, &m_ic_quit};
  for (int i = 0; i < 2; i++)
  {
    const Rect slot = {x + 100, y + 200 + i * 100.0f, 462, 86};
    DrawMenuButton(slot, labels[i], icons[i], m_sel_anim[i], 34, true);
    Hot(IR(slot), 3, i, [this, i](float, float) {
      m_choice_sel = i;
      Accept();
    });
  }
  RenderHints({{PsButton::Accept, "Aceptar"}, {PsButton::Back, "Cancelar"}}, IX(x + w - 30), IY(y + h + 16));
}

void App::RenderControls()
{
  DimScreen(m_emu.IsRunning() ? 0.75f : 0.5f);
  const float px = 40, py = 56, pw = 1060, ph = 950;
  Panel(px, py, pw, ph);
  m_r.Text(m_r.brush, "CONTROLES", IX(px + 44), IY(py + 20), IS(56), kWhite);

  struct Row
  {
    const char* action;
    PsButton b;
  };
  const Row rows[] = {
    {"Moverse", PsButton::LStick}, {"Cruceta", PsButton::DPad},      {"Cruz", PsButton::Cross},
    {"Círculo", PsButton::Circle}, {"Cuadrado", PsButton::Square},   {"Triángulo", PsButton::Triangle},
    {"L1", PsButton::L1},          {"R1", PsButton::R1},             {"L2", PsButton::L2},
    {"R2", PsButton::R2},          {"Start", PsButton::Start},       {"Select", PsButton::Select},
    {"Pausa del launcher", PsButton::Pause},
  };
  const float c0 = px + 52, c1 = px + 430, c2 = px + 650, c3 = px + 860;
  m_r.Text(m_r.ui, "BOTÓN DE PS1", IX(c0), IY(py + 104), IS(23), kDim);
  m_r.Text(m_r.ui, "TECLADO", IX(c1), IY(py + 104), IS(23), kDim);
  m_r.Text(m_r.ui, "XBOX", IX(c2), IY(py + 104), IS(23), kDim);
  m_r.Text(m_r.ui, "PLAYSTATION", IX(c3), IY(py + 104), IS(23), kDim);
  float y = py + 146;
  for (const Row& r : rows)
  {
    m_r.Text(m_r.ui, r.action, IX(c0), IY(y + 1), IS(28), kWhite);
    DrawGlyph(r.b, IX(c1), IY(y), IS(40), PromptStyle::Keyboard);
    DrawGlyph(r.b, IX(c2), IY(y), IS(40), PromptStyle::Xbox);
    DrawGlyph(r.b, IX(c3), IY(y), IS(40), PromptStyle::PlayStation);
    y += 54;
  }
  m_r.Text(m_r.ui,
           m_cfg.hardcore ? "F11 pantalla completa  ·  modo hardcore: sin F5 / F7"
                          : "F5 guardar · F7 cargar · F11 pantalla completa",
           IX(c0), IY(y + 14), IS(22), kDim);
  if (m_cfg.free_camera && !m_cfg.hardcore)
    m_r.Text(m_r.ui,
             m_cfg.kb_mouse ? "Ratón / joystick derecho: cámara libre  ·  R / R3: cámara del juego  ·  Clic central / L3: "
                              "fijar enemigo"
                            : "Joystick derecho: cámara libre  ·  R3: cámara del juego  ·  L3: fijar enemigo",
             IX(c0), IY(y + 44), IS(22), kDim);
  RenderHints({{PsButton::Back, "Volver"}}, IX(px + pw - 30), IY(py + ph - 64));
}

void App::RenderAchievements()
{
  DimScreen(0.5f);
  const float px = 40, py = 56, pw = 1100, ph = 950;
  Panel(px, py, pw, ph);
  m_r.Sprite(m_ic_ach, IX(px + 40), IY(py + 22), IS(62), IS(62), kWhite);
  m_r.Text(m_r.brush, "LOGROS", IX(px + 116), IY(py + 20), IS(56), kWhite);

  const Achievements::State st = m_ach.GetState();
  if (st == Achievements::State::Ready)
  {
    auto& list = m_ach.List();
    const int earned = m_ach.EarnedCount();
    m_r.Text(m_r.ui, Truncate(m_r.ui, m_ach.GameTitle(), IS(26), IS(pw - 420)), IX(px + pw - 40), IY(py + 24), IS(26),
             kGrey, Align::Right);
    const std::string prog = std::string(m_cfg.hardcore ? "Modo hardcore  ·  " : "") + std::to_string(earned) +
                             " de " + std::to_string(list.size()) + " logros (" +
                             std::to_string(m_ach.HardcoreCount()) + " hardcore)  ·  " +
                             std::to_string(m_ach.EarnedPoints()) + " / " + std::to_string(m_ach.TotalPoints()) +
                             " puntos";
    m_r.Text(m_r.ui, prog, IX(px + pw - 40), IY(py + 60), IS(24), kGold, Align::Right);
    const float bx = px + 44, by = py + 104, bw = pw - 88, bh = 10;
    m_r.Rect(IX(bx), IY(by), IS(bw), IS(bh), UiMode::Solid, {1, 1, 1, 0.1f});
    const float frac = list.empty() ? 0.0f : static_cast<float>(earned) / list.size();
    m_r.Rect(IX(bx), IY(by), IS(bw * frac), IS(bh), UiMode::Solid, {0.85f, 0.12f, 0.08f, 1});

    const float top = py + 132, view_h = 700, row_h = 88;
    // Las filas que asoman por arriba o por abajo se recortan contra el borde de la lista.
    m_r.SetClip(IX(px + 20), IY(top), IS(pw - 40), IS(view_h));
    for (size_t i = 0; i < list.size(); i++)
    {
      const float y = top + i * row_h - m_ach_scroll;
      if (y + row_h < top || y > top + view_h)
        continue;
      Achievement& a = list[i];
      const bool sel = static_cast<int>(i) == m_ach_sel;
      const Rect rr = IR({px + 30, y, pw - 60, row_h - 8});
      m_r.Rect(rr.x, rr.y, rr.w, rr.h, UiMode::Solid, sel ? Color{0.55f, 0.05f, 0.04f, 0.55f} : Color{1, 1, 1, 0.04f});
      Hot(rr, 6, static_cast<int>(i), [this, i](float, float) { m_ach_sel = static_cast<int>(i); });
      m_ach.LoadIcon(a, m_r);
      const float isz = IS(64);
      if (a.icon.id)
        m_r.Sprite(a.icon, rr.x + IS(10), rr.y + IS(8), isz, isz, {1, 1, 1, 1}, !a.unlocked);
      else
        m_r.Rect(rr.x + IS(10), rr.y + IS(8), isz, isz, UiMode::Solid, {0.2f, 0.2f, 0.2f, 1});
      const Color tc = a.unlocked ? kWhite : Color{0.72f, 0.7f, 0.68f, 1};
      const float text_w = rr.w - IS(92) - IS(150);
      FitText(m_r.ui, a.title, rr.x + IS(92), rr.y + IS(4), text_w, IS(27), tc, Align::Left, sel,
              10000 + static_cast<int>(i) * 2);
      FitText(m_r.ui, a.description, rr.x + IS(92), rr.y + IS(42), text_w, IS(21), kDim, Align::Left, sel,
              10000 + static_cast<int>(i) * 2 + 1);
      m_r.Text(m_r.ui, std::to_string(a.points) + " pts", rr.x + rr.w - IS(20), rr.y + IS(8), IS(23),
               a.unlocked ? kGold : kDim, Align::Right);
      std::string when = "Bloqueado";
      if (a.unlocked)
      {
        char date[32];
        const time_t t = static_cast<time_t>(a.unlock_time);
        std::strftime(date, sizeof(date), "%d/%m/%Y", std::localtime(&t));
        when = std::string(a.hardcore ? "Hardcore · " : "") + date;
      }
      m_r.Text(m_r.ui, when, rr.x + rr.w - IS(20), rr.y + IS(44), IS(19), kDim, Align::Right);
    }
    // Última fila: reiniciar los logros conseguidos (pide confirmación).
    {
      const size_t n = list.size();
      const float y = top + n * row_h - m_ach_scroll;
      if (y + row_h >= top && y <= top + view_h)
      {
        const bool sel = static_cast<int>(n) == m_ach_sel;
        const Rect rr = IR({px + 30, y, pw - 60, row_h - 8});
        if (sel)
          m_r.Sprite(m_btn_sel, rr.x - IS(8), rr.y - IS(4), rr.w + IS(16), rr.h + IS(8), {1, 1, 1, 0.95f});
        else
          m_r.Rect(rr.x, rr.y, rr.w, rr.h, UiMode::Solid, {0.4f, 0.03f, 0.02f, 0.25f});
        m_r.DrawIcon(Icon::Close, rr.x + IS(30), rr.y + IS(24), IS(32), sel ? kWhite : kRed);
        m_r.Text(m_r.ui, "Reiniciar mis logros", rr.x + IS(92), rr.y + IS(4), IS(27), sel ? kWhite : kGrey);
        m_r.Text(m_r.ui, "Borra los logros desbloqueados en este equipo (pide confirmación).", rr.x + IS(92),
                 rr.y + IS(42), IS(21), sel ? Color{1, 0.9f, 0.85f, 1} : kDim);
        Hot(rr, 6, static_cast<int>(n), [this, n](float, float) {
          m_ach_sel = static_cast<int>(n);
          Accept();
        });
      }
    }
    m_r.ClearClip();
    m_r.Text(m_r.ui, "Los logros se desbloquean sin conexión y se guardan sólo en este equipo.", IX(px + 44),
             IY(py + ph - 58), IS(21), kDim);
    RenderHints({{PsButton::Back, "Volver"}}, IX(px + pw - 30), IY(py + ph - 64));
    return;
  }

  // Sin set descargado: estado y, si procede, formulario de inicio de sesión.
  float y = py + 130;
  for (const std::string& line : Wrap(m_r.ui, m_ach.Status(), IS(27), IS(pw - 100)))
  {
    m_r.Text(m_r.ui, line, IX(px + 52), IY(y), IS(27), st == Achievements::State::Working ? kGold : kGrey);
    y += 38;
  }
  RenderHints({{PsButton::Back, "Volver"}}, IX(px + pw - 30), IY(py + ph - 64));
  if (st == Achievements::State::Working)
  {
    m_r.DrawIcon(Icon::Shuriken, IX(px + pw / 2 - 45), IY(py + 420), IS(90), kRed, 3.0f);
    return;
  }
  if (st != Achievements::State::NeedLogin)
    return;

  m_r.Text(m_r.ui, "Cuenta de RetroAchievements (retroachievements.org)", IX(px + 52), IY(y + 24), IS(26), kWhite);
  const char* labels[2] = {"Usuario", "Contraseña"};
  for (int i = 0; i < 2; i++)
  {
    const float fy = y + 76 + i * 104;
    const bool focus = m_login_focus == i;
    m_r.Text(m_r.ui, labels[i], IX(px + 52), IY(fy), IS(23), kGrey);
    const Rect field = IR({px + 52, fy + 34, 580, 56});
    m_r.Rect(field.x, field.y, field.w, field.h, UiMode::Solid,
             focus ? Color{0.13f, 0.03f, 0.03f, 0.95f} : Color{0.07f, 0.06f, 0.06f, 0.92f});
    m_r.Rect(field.x, field.y + field.h - IS(3), field.w, IS(3), UiMode::Solid,
             focus ? kRed : Color{0.4f, 0.4f, 0.4f, 1});
    const std::string shown = i == 0 ? m_login_user : std::string(m_login_pass.size(), '*');
    const bool caret = focus && std::fmod(m_time, 1.0f) < 0.55f;
    // Si el texto no cabe se muestra su parte final (donde se escribe).
    std::string visible = shown + (caret ? "|" : "");
    while (visible.size() > 1 && m_r.TextWidth(m_r.ui, visible, IS(29)) > field.w - IS(32))
      visible.erase(0, 1);
    m_r.Text(m_r.ui, visible, field.x + IS(16), field.y + IS(9), IS(29), kWhite);
    Hot(field, 4, i, [this, i](float, float) { m_login_focus = i; });
  }
  const Rect slot = {px + 46, y + 300, 462, 88};
  DrawMenuButton(slot, "DESCARGAR", &m_ic_ach, m_sel_anim[2], 34, true);
  Hot(IR(slot), 4, 2, [this](float, float) {
    m_login_focus = 2;
    StartLogin();
  });
  float ny = y + 420;
  for (const char* line : {"Tu contraseña sólo se usa para obtener un token de acceso y no se guarda.",
                           "Los logros se descargan una vez; después se juegan y desbloquean sin conexión."})
  {
    m_r.Text(m_r.ui, line, IX(px + 52), IY(ny), IS(21), kDim);
    ny += 32;
  }
}

void App::RenderGame()
{
  if (!m_emu.HasFrame())
    return;
  const float aspect = m_emu.AspectRatio();
  float w = static_cast<float>(m_w), h = w / aspect;
  if (h > m_h)
  {
    h = static_cast<float>(m_h);
    w = h * aspect;
  }
  const float x = (m_w - w) / 2, y = (m_h - h) / 2;
  // Laterales: la ilustración del launcher muy oscurecida, en lugar de franjas negras.
  if (m_cfg.side_art && x > 2.0f)
  {
    m_r.DrawBackground(m_bg, m_fire, m_bg_rect.x, m_bg_rect.y, m_bg_rect.w, m_bg_rect.h, 0.28f);
    // sombra suave junto a la imagen del juego
    const float sw = IS(40);
    m_r.Rect(x - sw, 0, sw, static_cast<float>(m_h), UiMode::Solid, {0, 0, 0, 0.5f});
    m_r.Rect(x + w, 0, sw, static_cast<float>(m_h), UiMode::Solid, {0, 0, 0, 0.5f});
  }
  const float u1 = static_cast<float>(m_emu.FrameWidth()) / m_emu.TextureWidth();
  const float v1 = static_cast<float>(m_emu.FrameHeight()) / m_emu.TextureHeight();
  if (m_emu.FlippedY())
    m_r.Image(m_emu.Texture(), x, y, w, h, 0, v1, u1, 0, m_cfg.smooth_filter);
  else
    m_r.Image(m_emu.Texture(), x, y, w, h, 0, 0, u1, v1, m_cfg.smooth_filter);

  // Marca sobre el enemigo fijado: punta roja que flota y late suavemente.
  m_mark_alpha += ((m_mark_visible && !m_paused ? 1.0f : 0.0f) - m_mark_alpha) * 0.25f;
  if (m_mark_alpha > 0.02f)
  {
    const float size = h * 0.045f * (1.0f + 0.06f * std::sin(m_time * 5.0f));
    const float mx = x + w * (0.5f + 0.5f * m_mark_x), my = y + h * (0.5f + 0.5f * m_mark_y) - h * 0.02f;
    const float bob = h * 0.008f * std::sin(m_time * 3.0f);
    m_r.DrawIcon(Icon::LockMark, mx - size / 2, my - size + bob, size, {0.86f, 0.1f, 0.08f, 0.9f * m_mark_alpha});
  }
}

void App::RenderPause()
{
  DimScreen(0.62f);
  const float px = 50, py = 90, pw = 620, ph = 860;
  Panel(px, py, pw, ph);
  m_r.Text(m_r.brush, "PAUSA", IX(px + 44), IY(py + 24), IS(60), kWhite);
  m_r.Text(m_r.kanji, "闇", IX(px + pw - 120), IY(py + 20), IS(86), {0.6f, 0.04f, 0.04f, 0.35f});
  const char* labels[kPauseItems] = {"REANUDAR", "GUARDAR", "CARGAR", "CONTROLES", "MENÚ PRINCIPAL", "SALIR"};
  const Texture* icons[kPauseItems] = {&m_ic_play, &m_ic_ach, &m_ic_ach, &m_ic_opt, &m_ic_opt, &m_ic_quit};
  for (int i = 0; i < kPauseItems; i++)
  {
    const Rect slot = {px + 70, py + 125 + i * 98.0f, 462, 88};
    const bool blocked = m_hardcore_session && (i == 1 || i == 2);
    DrawMenuButton(slot, labels[i], icons[i], m_sel_anim[i], 34, true, blocked ? 0.35f : 1.0f);
    Hot(IR(slot), 2, i, [this, i](float, float) { ActivatePause(i); });
  }
  if (m_cheats_active)
    m_r.Text(m_r.ui, "Logros en pausa: hay trucos activos", IX(px + 70), IY(py + ph - 64), IS(24), kDim);
  else if (m_ach.GetState() == Achievements::State::Ready)
    m_r.Text(m_r.ui,
             std::string(m_hardcore_session ? "MODO HARDCORE  ·  " : "") + "Logros: " +
               std::to_string(m_hardcore_session ? m_ach.HardcoreCount() : m_ach.EarnedCount()) + " / " +
               std::to_string(m_ach.List().size()),
             IX(px + 70), IY(py + ph - 64), IS(24), kGold);
  else if (m_hardcore_session)
    m_r.Text(m_r.ui, "MODO HARDCORE", IX(px + 70), IY(py + ph - 64), IS(24), kGold);
  RenderHints({{PsButton::Accept, "Aceptar"}, {PsButton::Back, "Reanudar"}}, m_w - IS(30), m_h - IS(60));
}

void App::RenderDialog()
{
  DimScreen(0.55f);
  const float w = 800, h = 380;
  const float x = std::max(40.0f, (m_w / m_s - w) / 2), y = (kImgH - h) / 2;
  Panel(x, y, w, h);
  m_r.Text(m_r.kanji, "刃", IX(x + w - 100), IY(y + 10), IS(80), {0.6f, 0.04f, 0.04f, 0.4f});
  float ty = y + 42;
  for (const std::string& line : Wrap(m_r.ui, m_dialog, IS(27), IS(w - 160)))
  {
    m_r.Text(m_r.ui, line, IX(x + 46), IY(ty), IS(27), kWhite);
    ty += 38;
  }
  RenderHints({{PsButton::Accept, "Aceptar"}}, IX(x + w - 40), IY(y + h - 66));
}

void App::RenderToast()
{
  if (m_toast_time <= 0)
    return;
  const float a = std::min(1.0f, m_toast_time * 2.0f);
  const float fs = IS(28);
  const std::string text = Truncate(m_r.ui, m_toast, fs, m_w * 0.7f);
  const float w = m_r.TextWidth(m_r.ui, text, fs) + IS(130);
  const float x = (m_w - w) / 2, y = m_h - IS(170);
  m_r.Sprite(m_btn_sel, x, y, w, IS(80), {1, 1, 1, a});
  m_r.Text(m_r.ui, text, m_w / 2.0f, y + IS(19), fs, WithAlpha(kWhite, a), Align::Center);
}

void App::RenderPopups()
{
  float y = IS(30);
  for (const Popup& p : m_popups)
  {
    Achievement* a = m_ach.Find(p.id);
    if (!a)
      continue;
    const float t = 6.0f - p.time;
    const float in = std::min(1.0f, t * 4.0f), out = std::min(1.0f, p.time * 2.0f);
    const float alpha = std::min(in, out);
    const float w = IS(580), h = IS(118);
    const float x = m_w - w - IS(30) + (1.0f - in) * IS(80);
    m_r.Rect(x, y, w, h, UiMode::Panel, {0.04f, 0.03f, 0.03f, 0.95f * alpha}, {0.85f, 0.65f, 0.2f, alpha});
    m_ach.LoadIcon(*a, m_r);
    if (a->icon.id)
      m_r.Sprite(a->icon, x + IS(22), y + IS(19), IS(80), IS(80), {1, 1, 1, alpha});
    m_r.Text(m_r.ui, "¡Logro desbloqueado!", x + IS(122), y + IS(14), IS(23), WithAlpha(kGold, alpha));
    m_r.Text(m_r.ui, Truncate(m_r.ui, a->title, IS(29), w - IS(230)), x + IS(122), y + IS(46), IS(29),
             WithAlpha(kWhite, alpha));
    m_r.Text(m_r.ui, std::to_string(a->points) + " pts", x + w - IS(22), y + IS(14), IS(23), WithAlpha(kGold, alpha),
             Align::Right);
    y += h + IS(12);
  }
}

// ------------------------------------------------------------------------------------------------

int App::Run(int argc, char** argv)
{
  std::string screenshot_screen = "menu";
  for (int i = 1; i < argc; i++)
  {
    const std::string a = argv[i];
    if (a == "--screenshot" && i + 1 < argc) m_screenshot_path = argv[++i];
    else if (a == "--time" && i + 1 < argc) m_screenshot_time = static_cast<float>(std::atof(argv[++i]));
    else if (a == "--screen" && i + 1 < argc) screenshot_screen = argv[++i];
  }

  m_cfg.Load();
  if (m_cfg.game_path.empty() || !Paths::FileExists(m_cfg.game_path))
  {
    const std::string found = AutoDetectGame();
    if (!found.empty())
    {
      m_cfg.game_path = found;
      m_cfg.Save();
    }
  }

  std::string error;
  if (!Init(&error))
  {
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Tenshu 2", error.c_str(), m_window);
    SDL_Log("%s", error.c_str());
    return 1;
  }

  if (!m_screenshot_path.empty())
  {
    // Modo prueba: captura un frame. Con --screen game|continue arranca el juego de verdad.
    auto capture = [this](const std::string& path) {
      std::vector<unsigned char> px(static_cast<size_t>(m_w) * m_h * 3);
      glPixelStorei(GL_PACK_ALIGNMENT, 1);
      glReadPixels(0, 0, m_w, m_h, GL_RGB, GL_UNSIGNED_BYTE, px.data());
      if (FILE* f = Platform::OpenFile(path.c_str(), "wb"))
      {
        std::fprintf(f, "P6\n%d %d\n255\n", m_w, m_h);
        for (int y = m_h - 1; y >= 0; y--)
          std::fwrite(&px[static_cast<size_t>(y) * m_w * 3], 1, static_cast<size_t>(m_w) * 3, f);
        std::fclose(f);
      }
    };
    m_ach_game_path = m_cfg.game_path;
    m_ach.SetGame(m_cfg.game_path);
    m_ws_serial_path = m_cfg.game_path;
    m_ws_available = Emulator::WidescreenPatch(Paths::ReadDiscSerial(m_cfg.game_path)) != nullptr;

    if (screenshot_screen == "game" || screenshot_screen == "continue")
    {
      if (const char* st = std::getenv("TENSHU_TEST_STYLE"))
        m_cfg.prompt_style = st;
      m_cfg.hardcore = std::getenv("TENSHU_TEST_HARDCORE") != nullptr; // pruebas: modo normal por defecto
      StartGame(screenshot_screen == "continue");
      if (m_modal == Modal::Dialog)
        SDL_Log("Error al arrancar: %s", m_dialog.c_str());
      // TENSHU_TEST_SCRIPT="12:start,15:cross,20:left:2.5" pulsa botones en segundos concretos
      // (el tercer campo, opcional, es cuánto tiempo se mantiene pulsado).
      struct Press
      {
        double t;
        int id;
        double hold;
      };
      std::vector<Press> script;
      if (const char* sc = std::getenv("TENSHU_TEST_SCRIPT"))
      {
        std::stringstream ss(sc);
        std::string item;
        while (std::getline(ss, item, ','))
        {
          const size_t c = item.find(':');
          std::string b = item.substr(c + 1);
          double hold = 0.15;
          if (const size_t c2 = b.find(':'); c2 != std::string::npos)
          {
            hold = std::atof(b.substr(c2 + 1).c_str());
            b = b.substr(0, c2);
          }
          int id = RETRO_DEVICE_ID_JOYPAD_START;
          if (b == "cross") id = RETRO_DEVICE_ID_JOYPAD_B;
          else if (b == "circle") id = RETRO_DEVICE_ID_JOYPAD_A;
          else if (b == "triangle") id = RETRO_DEVICE_ID_JOYPAD_X;
          else if (b == "down") id = RETRO_DEVICE_ID_JOYPAD_DOWN;
          else if (b == "up") id = RETRO_DEVICE_ID_JOYPAD_UP;
          else if (b == "right") id = RETRO_DEVICE_ID_JOYPAD_RIGHT;
          else if (b == "left") id = RETRO_DEVICE_ID_JOYPAD_LEFT;
          else if (b == "square") id = RETRO_DEVICE_ID_JOYPAD_Y;
          else if (b == "l1") id = RETRO_DEVICE_ID_JOYPAD_L;
          else if (b == "r1") id = RETRO_DEVICE_ID_JOYPAD_R;
          else if (b == "l2") id = RETRO_DEVICE_ID_JOYPAD_L2;
          else if (b == "r2") id = RETRO_DEVICE_ID_JOYPAD_R2;
          else if (b == "r3") id = RETRO_DEVICE_ID_JOYPAD_R3;
          else if (b == "l3") id = RETRO_DEVICE_ID_JOYPAD_L3;
          script.push_back({std::atof(item.substr(0, c).c_str()), id, hold});
        }
      }
      const double freq = static_cast<double>(SDL_GetPerformanceFrequency());
      const uint64_t start = SDL_GetPerformanceCounter();
      uint64_t last = start;
      int shot = 0;
      while ((SDL_GetPerformanceCounter() - start) / freq < m_screenshot_time)
      {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {}
        const uint64_t now = SDL_GetPerformanceCounter();
        const double el = (now - start) / freq;
        m_test_buttons = 0;
        // TENSHU_TEST_SAVE_AT="50" guarda un estado (en TENSHU_TEST_STATE) en ese segundo.
        if (const char* sv = std::getenv("TENSHU_TEST_SAVE_AT"))
          if (el >= std::atof(sv) && !m_test_saved)
            m_test_saved = SaveStateNow();
        // TENSHU_TEST_RSTICK="2:4:1:0,5:6:0:-1" mueve el joystick derecho (inicio:fin:x:y).
        // TENSHU_TEST_LSTICK igual para el joystick izquierdo.
        for (int side = 0; side < 2; side++)
        {
          float* stick = side ? m_test_lstick : m_test_rstick;
          stick[0] = stick[1] = 0;
          if (const char* rs = std::getenv(side ? "TENSHU_TEST_LSTICK" : "TENSHU_TEST_RSTICK"))
          {
            std::stringstream ss(rs);
            std::string item;
            while (std::getline(ss, item, ','))
            {
              float t0 = 0, t1 = 0, x = 0, y = 0;
              if (std::sscanf(item.c_str(), "%f:%f:%f:%f", &t0, &t1, &x, &y) == 4 && el >= t0 && el < t1)
              {
                stick[0] = x;
                stick[1] = y;
              }
            }
          }
        }
        // TENSHU_TEST_MOUSE="1:2:5:0" mueve el ratón (px por actualización) entre los segundos 1 y 2.
        if (const char* ms = std::getenv("TENSHU_TEST_MOUSE"))
        {
          float t0 = 0, t1 = 0, mx = 0, my = 0;
          if (std::sscanf(ms, "%f:%f:%f:%f", &t0, &t1, &mx, &my) == 4 && el >= t0 && el < t1)
          {
            m_look_dx += mx;
            m_look_dy += my;
          }
        }
        // TENSHU_TEST_STYLE_AT="55:xbox" cambia el estilo de iconos en plena partida.
        if (const char* sa = std::getenv("TENSHU_TEST_STYLE_AT"))
          if (el >= std::atof(sa) && std::strchr(sa, ':'))
            m_cfg.prompt_style = std::strchr(sa, ':') + 1;
        for (const Press& p : script)
          if (el >= p.t && el < p.t + p.hold)
            m_test_buttons |= static_cast<uint16_t>(1u << p.id);
        Update(static_cast<float>((now - last) / freq));
        last = now;
        Render();
        const double every = std::getenv("TENSHU_TEST_SHOT_EVERY") ? std::atof(std::getenv("TENSHU_TEST_SHOT_EVERY")) : 5.0;
        if (el > (shot + 1) * every)
        {
          shot++;
          SDL_Log("t=%.0fs frames_emu=%llu fps_obj=%.2f frame=%dx%d", el,
                  static_cast<unsigned long long>(m_emu.FramesRun()), m_emu.TargetFps(), m_emu.FrameWidth(),
                  m_emu.FrameHeight());
          capture(m_screenshot_path + "." + std::to_string(shot) + ".ppm");
          if (std::getenv("TENSHU_TEST_STATE_EACH"))
            m_emu.SaveState(m_screenshot_path + "." + std::to_string(shot) + ".state");
          if (std::getenv("TENSHU_DUMP_VRAM"))
            m_emu.DumpVram(m_screenshot_path + "." + std::to_string(shot) + ".vram");
          if (std::getenv("TENSHU_DUMP_RAM"))
          {
            size_t n = 0;
            if (const uint8_t* ram = m_emu.SystemRam(&n))
              if (FILE* f = Platform::OpenFile((m_screenshot_path + "." + std::to_string(shot) + ".ram").c_str(), "wb"))
              {
                std::fwrite(ram, 1, n, f);
                std::fclose(f);
              }
          }
        }
        SDL_GL_SwapWindow(m_window);
        const double fr = (SDL_GetPerformanceCounter() - now) / freq;
        if (fr < 1.0 / 240.0)
          SDL_Delay(static_cast<Uint32>((1.0 / 240.0 - fr) * 1000.0) + 1);
      }
      SDL_Log("Emulación: %llu frames en %.1f s (objetivo %.2f fps)",
              static_cast<unsigned long long>(m_emu.FramesRun()), m_screenshot_time, m_emu.TargetFps());
      if (std::getenv("TENSHU_TEST_PAUSE"))
      {
        m_paused = true;
        m_emu.SetPaused(true);
        for (float& f : m_sel_anim) f = 0;
        m_sel_anim[0] = 1;
      }
      Render();
    }
    else
    {
      m_time = m_screenshot_time;
      m_fade = 1.0f;
      m_intro_t = kIntroLen;
      if (const char* it = std::getenv("TENSHU_TEST_INTRO"))
        m_intro_t = static_cast<float>(std::atof(it));
      if (const char* ot = std::getenv("TENSHU_TEST_OUTRO"))
        m_outro_t = static_cast<float>(std::atof(ot));
      if (screenshot_screen == "options") m_screen = Screen::Options;
      if (screenshot_screen == "controls") m_screen = Screen::Controls;
      if (screenshot_screen == "remap") m_screen = Screen::Remap;
      if (screenshot_screen == "credits") m_screen = Screen::Credits;
      if (screenshot_screen == "cheats")
      {
        m_screen = Screen::Cheats;
        m_cheat_list = LoadCheats(Paths::AssetsDir(), Paths::ReadDiscSerial(m_cfg.game_path));
        if (const char* cs = std::getenv("TENSHU_TEST_CHEATSEL"))
        {
          m_cheat_sel = std::atoi(cs);
          const int n = static_cast<int>(m_cheat_list.size());
          if (m_cheat_sel < n)
            m_cheat_scroll = std::clamp(m_cheat_sel * 56.0f - 224.0f, 0.0f, std::max(0.0f, n * 56.0f - 560.0f));
        }
      }
      if (const char* cs = std::getenv("TENSHU_TEST_CREDITS"))
        m_credits_scroll = static_cast<float>(std::atof(cs));
      if (screenshot_screen == "confirmreset")
      {
        m_screen = Screen::Achievements;
        m_modal = Modal::ConfirmReset;
      }
      if (const char* as = std::getenv("TENSHU_TEST_ACHSEL"))
      {
        m_ach_sel = std::atoi(as);
        m_ach_scroll = m_ach_scroll_target = std::max(0.0f, (m_ach_sel + 1) * 88.0f - 700.0f);
      }
      if (const char* dev = std::getenv("TENSHU_TEST_DEVICE"))
        m_remap_device = std::atoi(dev);
      if (const char* rs = std::getenv("TENSHU_TEST_REMAPSEL"))
        m_remap_sel = std::atoi(rs);
      if (std::getenv("TENSHU_TEST_WAIT"))
      {
        m_remap_wait = kRemapOrder[m_remap_sel];
        m_remap_timer = 4.2f;
      }
      if (screenshot_screen == "achievements") m_screen = Screen::Achievements;
      if (screenshot_screen == "choice") m_modal = Modal::PlayChoice;
      if (screenshot_screen == "dialog") ShowDialog("Ejemplo de diálogo.\nSegunda línea de texto.");
      if (const char* st = std::getenv("TENSHU_TEST_STYLE"))
        m_cfg.prompt_style = st;
      if (const char* pg = std::getenv("TENSHU_TEST_PAGE"))
        m_opt_page = std::atoi(pg);
      if (const char* sel = std::getenv("TENSHU_TEST_SEL"))
        m_sel = m_opt_sel = m_choice_sel = std::atoi(sel);
      int active = m_sel;
      if (m_modal == Modal::PlayChoice)
        active = m_choice_sel;
      else if (m_screen == Screen::Options)
        active = m_opt_page < 0 ? m_opt_sel : -1;
      for (int i = 0; i < 8; i++)
        m_sel_anim[i] = (i == active) ? 1.0f : 0.0f;
      // Para la marquesina: el texto empezó a desplazarse hace TENSHU_TEST_MARQUEE segundos.
      if (const char* mq = std::getenv("TENSHU_TEST_MARQUEE"))
      {
        Render(); // registra la fila activa
        m_marquee_start = m_time - static_cast<float>(std::atof(mq));
      }
      Render();
    }
    capture(m_screenshot_path);
    Shutdown();
    return 0;
  }

  uint64_t last = SDL_GetPerformanceCounter();
  while (m_running)
  {
    SDL_Event e;
    while (SDL_PollEvent(&e))
      HandleEvent(e);

    const uint64_t now = SDL_GetPerformanceCounter();
    const float dt = static_cast<float>(now - last) / static_cast<float>(SDL_GetPerformanceFrequency());
    last = now;
    Update(std::min(dt, 0.1f));
    Render();
    SDL_GL_SwapWindow(m_window);

    // Respaldo por si el vsync no bloquea (ventana oculta, algunos compositores): máx. ~240 fps.
    const double frame = (SDL_GetPerformanceCounter() - now) / static_cast<double>(SDL_GetPerformanceFrequency());
    if (frame < 1.0 / 240.0)
      SDL_Delay(static_cast<Uint32>((1.0 / 240.0 - frame) * 1000.0) + 1);
  }

  m_cfg.Save();
  Shutdown();
  return 0;
}

int main(int argc, char** argv)
{
  // Prueba: TENSHU_PREVIEW_BUTTONS=archivo.ppm guarda los iconos generados para el juego
  // (columnas: Xbox, teclado; con la configuración actual) y sale.
  if (const char* out = std::getenv("TENSHU_PREVIEW_BUTTONS"))
  {
    Config cfg;
    cfg.Load();
    for (int id = 0; id < kPsButtons; id++)
      cfg.key_bind[id] = cfg.KeyBind(id);
    const auto xb = PreviewGameButtons(GameButtonStyle::Xbox, cfg);
    const auto kb = PreviewGameButtons(GameButtonStyle::Keyboard, cfg);
    const int h = static_cast<int>(xb.size() / 16);
    if (FILE* f = Platform::OpenFile(out, "wb"))
    {
      std::fprintf(f, "P6 %d %d 255\n", 34, h);
      for (int y = 0; y < h; y++)
        for (int x = 0; x < 34; x++)
        {
          const uint32_t c = x < 16 ? xb[y * 16 + x] : x < 18 ? 0 : kb[y * 16 + x - 18];
          const unsigned char px[3] = {static_cast<unsigned char>(c), static_cast<unsigned char>(c >> 8),
                                       static_cast<unsigned char>(c >> 16)};
          std::fwrite(px, 1, 3, f);
        }
      std::fclose(f);
    }
    return 0;
  }
  App app;
  return app.Run(argc, argv);
}
