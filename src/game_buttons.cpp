#include "game_buttons.h"
#include "i18n.h"

#include <SDL.h>
#include "libretro.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <string>

namespace {

// Botones de PS1 (RETRO_DEVICE_ID_JOYPAD_*) que muestran los iconos del juego.
constexpr unsigned kCross = RETRO_DEVICE_ID_JOYPAD_B, kCircle = RETRO_DEVICE_ID_JOYPAD_A,
                   kSquare = RETRO_DEVICE_ID_JOYPAD_Y, kTriangle = RETRO_DEVICE_ID_JOYPAD_X,
                   kStart = RETRO_DEVICE_ID_JOYPAD_START;

constexpr uint16_t kKeep = 0xFFFF; // en un parche: deja el píxel original

// Índices de la paleta de cada textura con los que se dibujan los iconos.
struct Pal
{
  uint16_t clear;   // fuera del icono (kKeep = respetar el fondo)
  uint16_t fill[5]; // relleno del disco, de claro a oscuro
  uint16_t outline, white, keyfill;
  uint16_t green, red, blue, yellow; // A, B, X, Y de Xbox
};

struct Spot
{
  uint16_t x, y; // en píxeles dentro de la subida
  unsigned button;
};

// Una textura del juego con iconos de botones (identificada por el hash de su subida).
struct Target
{
  uint64_t lo, hi;
  uint32_t vx, vy;     // sitio de la VRAM donde la pone el juego
  uint16_t w, h, bpp;  // tamaño en palabras de 16 bits y bits por píxel
  bool fixed;          // el juego no usa ese sitio para otra cosa
  int size;            // tamaño de los iconos: 16 o 8 píxeles
  Pal pal;
  std::vector<Spot> spots;
};

// Colores que se cambian en la paleta de una textura (para tener rojo, amarillo o blanco).
struct ClutFix
{
  uint64_t lo, hi;
  uint32_t vx, vy;
  uint16_t w;
  std::vector<std::pair<uint16_t, uint16_t>> entries; // índice, color de 15 bits
};

// Color de PS1 (15 bits, con el bit de semitransparencia como en las paletas originales).
constexpr uint16_t Rgb15(int r, int g, int b)
{
  return static_cast<uint16_t>((r >> 3) | ((g >> 3) << 5) | ((b >> 3) << 10) | 0x8000);
}
constexpr uint16_t kRgbRed = Rgb15(224, 64, 56), kRgbYellow = Rgb15(232, 200, 56),
                   kRgbWhite = Rgb15(248, 248, 248);

// Texturas de Tenchu 2 (PAL España; localizadas volcando la VRAM). Sólo se guardan hashes,
// posiciones e índices de color: ningún píxel del juego.
const std::vector<Target>& Targets()
{
  static const std::vector<Target> targets = {
    // Tira de iconos de los menús (16x16, 4 bits): START, ○, ✕, □, △.
    {0xa2dd40cfc0822658ull, 0x63040010f127bbf2ull, 1000, 256, 4, 80, 4, true, 16,
     {0xf, {0x8, 0x9, 0xa, 0xb, 0xc}, 0xe, 0x1, 0xd, 0x4, 0x6, 0x2, 0x0},
     {{0, 0, kStart}, {0, 16, kCircle}, {0, 32, kCross}, {0, 48, kSquare}, {0, 64, kTriangle}}},
    // Fondo del menú de objetos (320x256, 8 bits) con «✕ Ad.  △ Quitar  ▶ INIC.» dibujados.
    {0x15f4fbba93b28a3full, 0x676a26978e083906ull, 320, 256, 160, 256, 8, false, 16,
     {kKeep, {0x76, 0x77, 0x78, 0x79, 0x7a}, 0x7d, 0x65, 0x7c, 0x10, 130, 0x04, 137},
     {{18, 208, kCross}, {18, 226, kTriangle}, {85, 227, kStart}}},
    // Textos del curso de entrenamiento (4 bits), con iconos de 8x8 dentro de las frases.
    {0x4b93999bfb6f06a4ull, 0x0e61faf7195a772full, 320, 384, 64, 90, 4, false, 8,
     {0xe, {0xa, 0xa, 0xa, 0xb, 0xb}, 0xc, 0x4, 0xd, 0xf, 0x3, 0x1, 0x0},
     {{131, 11, kCross}, {125, 31, kTriangle}, {186, 50, kSquare}, {149, 61, kCircle}}},
    {0x4d9599e1e98c3264ull, 0xd4cb06573fb5baa2ull, 960, 0, 64, 230, 4, false, 8,
     {0xe, {0xa, 0xa, 0xa, 0xb, 0xb}, 0xc, 0x4, 0xd, 0x2, 0x3, 0x0, 0xf},
     {{80, 11, kCircle},
      {96, 41, kCircle},
      {182, 51, kCross},
      {223, 61, kCross},
      {115, 71, kCircle},
      {197, 71, kCross},
      {162, 141, kTriangle},
      {143, 201, kCross},
      {142, 211, kCross},
      {113, 221, kCross}}},
  };
  return targets;
}

const std::vector<ClutFix>& ClutFixes()
{
  static const std::vector<ClutFix> fixes = {
    // Tira de los menús: rosa de □ → amarillo, 1 → blanco, naranja de ○ → rojo.
    {0xb32eb74bb9699d93ull, 0x0bbf484bedae0be1ull, 1008, 511, 16, {{0, kRgbYellow}, {1, kRgbWhite}, {6, kRgbRed}}},
    // Menú de objetos: dos colores que la imagen no usa.
    {0x3ab743c6fb1c2b32ull, 0x667aa1dfa84eff77ull, 320, 511, 256, {{130, kRgbRed}, {137, kRgbYellow}}},
    // Textos del entrenamiento.
    {0xe836450f13f22e12ull, 0x13463f212f49774bull, 976, 255, 16, {{0, kRgbYellow}, {3, kRgbRed}}},
    {0x5b3caf4ef927dd0cull, 0x9720d14698d0b37full, 960, 255, 16, {{3, kRgbRed}, {0xf, kRgbYellow}}},
  };
  return fixes;
}

// Fuente de 5x7 (letras grandes) y de 3x5 (etiquetas de varias letras), en mayúsculas y cifras.
const char* const kFont5[36][7] = {
  {"01110", "10001", "10001", "11111", "10001", "10001", "10001"}, // A
  {"11110", "10001", "10001", "11110", "10001", "10001", "11110"}, // B
  {"01110", "10001", "10000", "10000", "10000", "10001", "01110"}, // C
  {"11110", "10001", "10001", "10001", "10001", "10001", "11110"}, // D
  {"11111", "10000", "10000", "11110", "10000", "10000", "11111"}, // E
  {"11111", "10000", "10000", "11110", "10000", "10000", "10000"}, // F
  {"01110", "10001", "10000", "10111", "10001", "10001", "01111"}, // G
  {"10001", "10001", "10001", "11111", "10001", "10001", "10001"}, // H
  {"01110", "00100", "00100", "00100", "00100", "00100", "01110"}, // I
  {"00111", "00010", "00010", "00010", "00010", "10010", "01100"}, // J
  {"10001", "10010", "10100", "11000", "10100", "10010", "10001"}, // K
  {"10000", "10000", "10000", "10000", "10000", "10000", "11111"}, // L
  {"10001", "11011", "10101", "10101", "10001", "10001", "10001"}, // M
  {"10001", "10001", "11001", "10101", "10011", "10001", "10001"}, // N
  {"01110", "10001", "10001", "10001", "10001", "10001", "01110"}, // O
  {"11110", "10001", "10001", "11110", "10000", "10000", "10000"}, // P
  {"01110", "10001", "10001", "10001", "10101", "10010", "01101"}, // Q
  {"11110", "10001", "10001", "11110", "10100", "10010", "10001"}, // R
  {"01111", "10000", "10000", "01110", "00001", "00001", "11110"}, // S
  {"11111", "00100", "00100", "00100", "00100", "00100", "00100"}, // T
  {"10001", "10001", "10001", "10001", "10001", "10001", "01110"}, // U
  {"10001", "10001", "10001", "10001", "10001", "01010", "00100"}, // V
  {"10001", "10001", "10001", "10101", "10101", "10101", "01010"}, // W
  {"10001", "10001", "01010", "00100", "01010", "10001", "10001"}, // X
  {"10001", "10001", "01010", "00100", "00100", "00100", "00100"}, // Y
  {"11111", "00001", "00010", "00100", "01000", "10000", "11111"}, // Z
  {"01110", "10001", "10011", "10101", "11001", "10001", "01110"}, // 0
  {"00100", "01100", "00100", "00100", "00100", "00100", "01110"}, // 1
  {"01110", "10001", "00001", "00010", "00100", "01000", "11111"}, // 2
  {"11111", "00010", "00100", "00010", "00001", "10001", "01110"}, // 3
  {"00010", "00110", "01010", "10010", "11111", "00010", "00010"}, // 4
  {"11111", "10000", "11110", "00001", "00001", "10001", "01110"}, // 5
  {"00110", "01000", "10000", "11110", "10001", "10001", "01110"}, // 6
  {"11111", "00001", "00010", "00100", "01000", "01000", "01000"}, // 7
  {"01110", "10001", "10001", "01110", "10001", "10001", "01110"}, // 8
  {"01110", "10001", "10001", "01111", "00001", "00010", "01100"}, // 9
};
const char* const kFont3[36][5] = {
  {"010", "101", "111", "101", "101"}, {"110", "101", "110", "101", "110"}, {"011", "100", "100", "100", "011"},
  {"110", "101", "101", "101", "110"}, {"111", "100", "110", "100", "111"}, {"111", "100", "110", "100", "100"},
  {"011", "100", "101", "101", "011"}, {"101", "101", "111", "101", "101"}, {"111", "010", "010", "010", "111"},
  {"001", "001", "001", "101", "010"}, {"101", "101", "110", "101", "101"}, {"100", "100", "100", "100", "111"},
  {"101", "111", "111", "101", "101"}, {"110", "101", "101", "101", "101"}, {"010", "101", "101", "101", "010"},
  {"110", "101", "110", "100", "100"}, {"010", "101", "101", "110", "011"}, {"110", "101", "110", "101", "101"},
  {"011", "100", "010", "001", "110"}, {"111", "010", "010", "010", "010"}, {"101", "101", "101", "101", "111"},
  {"101", "101", "101", "101", "010"}, {"101", "101", "111", "111", "101"}, {"101", "101", "010", "101", "101"},
  {"101", "101", "010", "010", "010"}, {"111", "001", "010", "100", "111"}, {"111", "101", "101", "101", "111"},
  {"010", "110", "010", "010", "111"}, {"110", "001", "010", "100", "111"}, {"110", "001", "010", "001", "110"},
  {"101", "101", "111", "001", "001"}, {"111", "100", "110", "001", "110"}, {"011", "100", "110", "101", "010"},
  {"111", "001", "010", "010", "010"}, {"010", "101", "010", "101", "010"}, {"010", "101", "011", "001", "110"},
};

int GlyphIndex(char c)
{
  if (c >= 'A' && c <= 'Z')
    return c - 'A';
  if (c >= '0' && c <= '9')
    return 26 + (c - '0');
  return -1;
}


// Lienzo de un icono (hasta 16x16), en índices de la paleta de su textura.
struct Canvas
{
  int size;
  uint16_t px[16][16];
  void Put(int x, int y, uint16_t c)
  {
    if (x >= 0 && x < size && y >= 0 && y < size)
      px[y][x] = c;
  }
};

const char* const kMini[][5] = {
  {"001", "001", "101", "111", "100"}, // 0 Enter
  {"000", "000", "000", "101", "111"}, // 1 Espacio
  {"010", "111", "101", "010", "010"}, // 2 Mayús
  {"010", "111", "010", "010", "010"}, // 3 arriba
  {"010", "010", "010", "111", "010"}, // 4 abajo
  {"001", "011", "111", "011", "001"}, // 5 izquierda
  {"100", "110", "111", "110", "100"}, // 6 derecha
  {"111", "000", "111", "000", "111"}, // 7 menú (≡)
};

// Etiqueta de un botón: letra(s) o un símbolo de kMini.
struct Label
{
  std::string text;
  int mini = -1;
  int face = -1; // botón de cara de Xbox: 0 A, 1 B, 2 X, 3 Y
  int mouse = -1; // ratón: 0 clic izquierdo, 1 derecho, 2 central, 3 rueda arriba, 4 rueda abajo
};

Label KeyLabel(int scancode)
{
  if (scancode >= kMouseLeft && scancode <= kWheelDown)
    return {"", -1, -1, scancode - kMouseLeft};
  switch (scancode)
  {
    case SDL_SCANCODE_UP: return {"", 3};
    case SDL_SCANCODE_DOWN: return {"", 4};
    case SDL_SCANCODE_LEFT: return {"", 5};
    case SDL_SCANCODE_RIGHT: return {"", 6};
    case SDL_SCANCODE_RETURN:
    case SDL_SCANCODE_KP_ENTER: return {"ENT", 0};
    case SDL_SCANCODE_SPACE: return {"SPC", 1};
    case SDL_SCANCODE_LSHIFT:
    case SDL_SCANCODE_RSHIFT: return {LangEn() ? "SHF" : "MAY", 2};
    case SDL_SCANCODE_ESCAPE: return {"ESC"};
    case SDL_SCANCODE_BACKSPACE: return {"DEL"};
    case SDL_SCANCODE_TAB: return {"TAB"};
    case SDL_SCANCODE_LCTRL:
    case SDL_SCANCODE_RCTRL: return {"CTL"};
    case SDL_SCANCODE_LALT:
    case SDL_SCANCODE_RALT: return {"ALT"};
    default: break;
  }
  std::string out;
  if (const char* name = SDL_GetScancodeName(static_cast<SDL_Scancode>(scancode)))
    for (const char* p = name; *p && out.size() < 3; p++)
      if (std::isalnum(static_cast<unsigned char>(*p)))
        out += static_cast<char>(std::toupper(static_cast<unsigned char>(*p)));
  return {out.empty() ? "?" : out};
}

Label PadLabel(int bind)
{
  if (bind >= kPadAxisBase)
    return {(bind - kPadAxisBase) == SDL_CONTROLLER_AXIS_TRIGGERLEFT ? "LT" : "RT"};
  switch (bind)
  {
    case SDL_CONTROLLER_BUTTON_A: return {"A", -1, 0};
    case SDL_CONTROLLER_BUTTON_B: return {"B", -1, 1};
    case SDL_CONTROLLER_BUTTON_X: return {"X", -1, 2};
    case SDL_CONTROLLER_BUTTON_Y: return {"Y", -1, 3};
    case SDL_CONTROLLER_BUTTON_LEFTSHOULDER: return {"LB"};
    case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER: return {"RB"};
    case SDL_CONTROLLER_BUTTON_LEFTSTICK: return {"LS"};
    case SDL_CONTROLLER_BUTTON_RIGHTSTICK: return {"RS"};
    case SDL_CONTROLLER_BUTTON_BACK: return {"VW"};
    case SDL_CONTROLLER_BUTTON_START: return {"MEN", 7};
    case SDL_CONTROLLER_BUTTON_DPAD_UP: return {"", 3};
    case SDL_CONTROLLER_BUTTON_DPAD_DOWN: return {"", 4};
    case SDL_CONTROLLER_BUTTON_DPAD_LEFT: return {"", 5};
    case SDL_CONTROLLER_BUTTON_DPAD_RIGHT: return {"", 6};
    default: return {"?"};
  }
}

void DrawGlyph3(Canvas& c, const char* const rows[5], int x0, int y0, uint16_t color)
{
  for (int y = 0; y < 5; y++)
    for (int x = 0; x < 3; x++)
      if (rows[y][x] == '1')
        c.Put(x0 + x, y0 + y, color);
}

void DrawChar3(Canvas& c, char ch, int x0, int y0, uint16_t color)
{
  const int g = GlyphIndex(ch);
  if (g >= 0)
    DrawGlyph3(c, kFont3[g], x0, y0, color);
}

// Letra grande en negrita (5x7 desplazada un píxel: 6x7), con sombra.
void DrawBigLetter(Canvas& c, char ch, uint16_t color, uint16_t shadow)
{
  const int g = GlyphIndex(ch);
  if (g < 0)
    return;
  const int ox = 5, oy = 4;
  for (int pass = 0; pass < 2; pass++) // 0: sombra (un píxel abajo a la derecha), 1: letra
  {
    const int s = pass ? 0 : 1;
    for (int y = 0; y < 7; y++)
      for (int x = 0; x < 5; x++)
        if (kFont5[g][y][x] == '1')
          for (int b = 0; b < 2; b++)
            c.Put(ox + x + b + s, oy + y + s, pass ? color : shadow);
  }
}

// Símbolo de kMini ampliado al doble (para iconos de 16).
void DrawMiniBig(Canvas& c, int mini, uint16_t color)
{
  for (int y = 0; y < 5; y++)
    for (int x = 0; x < 3; x++)
      if (kMini[mini][y][x] == '1')
        for (int dy = 0; dy < 2; dy++)
          for (int dx = 0; dx < 2; dx++)
            c.Put(5 + x * 2 + dx, 3 + y * 2 + dy, color);
}

void DrawDisc(Canvas& c, const Pal& p)
{
  const float mid = c.size / 2.0f, outer = c.size / 2.0f - 0.4f, inner = outer - 1.0f;
  for (int y = 0; y < c.size; y++)
    for (int x = 0; x < c.size; x++)
    {
      const float dx = x + 0.5f - mid, dy = y + 0.5f - mid, d2 = dx * dx + dy * dy;
      if (d2 > outer * outer)
        continue;
      c.px[y][x] = d2 > inner * inner ? p.outline : p.fill[std::min(4, (x + y) * 5 / (2 * c.size))];
    }
}

void DrawKeycap(Canvas& c, const Pal& p)
{
  const int n = c.size, top = n == 16 ? 1 : 0, bottom = n == 16 ? 14 : 7;
  for (int y = top; y <= bottom; y++)
    for (int x = 0; x < n; x++)
    {
      const bool corner = (x == 0 || x == n - 1) && (y == top || y == bottom);
      if (corner)
        continue;
      const bool edge = x == 0 || x == n - 1 || y == top || y == bottom;
      c.px[y][x] = edge ? p.white : p.keyfill;
    }
}

// Ratón: cuerpo ovalado con el botón indicado relleno (o la rueda, con una flecha).
void DrawMouse(Canvas& c, const Pal& p, int which)
{
  const float n = static_cast<float>(c.size), cx = n / 2.0f, rx = n * 0.36f, ry = n * 0.47f, cy = n / 2.0f;
  const float split = c.size == 16 ? 6.5f : 3.5f;
  for (int y = 0; y < c.size; y++)
    for (int x = 0; x < c.size; x++)
    {
      const float dx = (x + 0.5f - cx) / rx, dy = (y + 0.5f - cy) / ry, d = dx * dx + dy * dy;
      if (d > 1.0f)
        continue;
      const bool edge = d > (c.size == 16 ? 0.62f : 0.45f);
      const bool top = y + 0.5f < split;
      const bool left = x + 0.5f < cx;
      bool fill = false;
      if (which == 0) fill = top && left;
      if (which == 1) fill = top && !left;
      c.px[y][x] = edge || fill ? p.white : p.keyfill;
    }
  // Separación entre botones y rueda.
  for (int x = 0; x < c.size; x++)
    if (c.px[static_cast<int>(split)][x] == p.keyfill)
      c.px[static_cast<int>(split)][x] = p.white;
  if (which >= 2)
  {
    const int wx = c.size / 2 - (c.size == 16 ? 1 : 0), w = c.size == 16 ? 2 : 1;
    for (int y = c.size == 16 ? 2 : 1; y < split; y++)
      for (int k = 0; k < w; k++)
        c.Put(wx + k, y, which == 2 ? p.white : p.outline);
    if (which >= 3 && c.size == 16)
      DrawGlyph3(c, kMini[which == 3 ? 3 : 4], 6, 8, p.white); // flecha arriba / abajo bajo la rueda
  }
}

void DrawIcon(Canvas& c, const Pal& p, GameButtonStyle style, const Config& cfg, unsigned button)
{
  for (auto& row : c.px)
    for (auto& v : row)
      v = p.clear;
  const Label l = style == GameButtonStyle::Keyboard ? KeyLabel(cfg.key_bind[button]) : PadLabel(cfg.pad_bind[button]);
  const bool big = c.size == 16;
  if (l.mouse >= 0)
  {
    DrawMouse(c, p, l.mouse);
    return;
  }
  if (l.face >= 0)
  {
    const uint16_t colors[4] = {p.green, p.red, p.blue, p.yellow};
    if (big)
    {
      DrawDisc(c, p);
      DrawBigLetter(c, l.text[0], colors[l.face], p.outline);
    }
    else
    {
      // A 8 píxeles la letra de color no se lee: disco del color del botón y letra blanca.
      Pal colored = p;
      for (auto& f : colored.fill)
        f = colors[l.face];
      DrawDisc(c, colored);
      DrawChar3(c, l.text[0], 3, 1, p.white);
    }
    return;
  }
  if (l.mini == 7) // botón Menú de Xbox: disco con ≡
  {
    DrawDisc(c, p);
    if (big)
      DrawMiniBig(c, 7, p.white);
    else
      DrawGlyph3(c, kMini[7], 3, 1, p.white);
    return;
  }
  DrawKeycap(c, p);
  if (big)
  {
    if (l.mini >= 3)
      DrawMiniBig(c, l.mini, p.white);
    else if (l.text.size() == 1)
      DrawBigLetter(c, l.text[0], p.white, p.keyfill);
    else
    {
      const int n = static_cast<int>(std::min<size_t>(l.text.size(), 3));
      int x0 = (16 - (n * 4 - 1)) / 2;
      for (int i = 0; i < n; i++, x0 += 4)
        DrawChar3(c, l.text[i], x0, 6, p.white);
    }
  }
  else if (l.mini >= 0)
    DrawGlyph3(c, kMini[l.mini], 3, 1, p.white);
  else
    DrawChar3(c, l.text.empty() ? '?' : l.text[0], 3, 1, p.white);
}

void Push16(std::vector<uint8_t>& out, uint16_t v)
{
  out.push_back(static_cast<uint8_t>(v));
  out.push_back(static_cast<uint8_t>(v >> 8));
}

void Push64(std::vector<uint8_t>& out, uint64_t v)
{
  for (int i = 0; i < 8; i++)
    out.push_back(static_cast<uint8_t>(v >> (8 * i)));
}

void Header(std::vector<uint8_t>& out, uint64_t lo, uint64_t hi, uint16_t w, uint16_t h, uint16_t bpp,
            uint16_t rects)
{
  out.insert(out.end(), {'K', 'V', 'P', '2'});
  Push64(out, lo);
  Push64(out, hi);
  Push16(out, w);
  Push16(out, h);
  Push16(out, bpp);
  Push16(out, kKeep);
  Push16(out, rects);
}

} // namespace

std::vector<GameUpload> GameButtonUploads()
{
  std::vector<GameUpload> out;
  for (const Target& t : Targets())
    out.push_back({t.lo, t.hi, t.vx, t.vy, t.fixed});
  for (const ClutFix& f : ClutFixes())
    out.push_back({f.lo, f.hi, f.vx, f.vy, f.vy == 511 && f.vx == 1008});
  return out;
}

std::vector<uint8_t> BuildGameButtonPatches(GameButtonStyle style, const Config& cfg)
{
  // Con los iconos originales se envían los mismos rectángulos con todos los píxeles en kKeep: así
  // el núcleo restaura el original justo ahí (por ejemplo, tras cargar un estado con otros iconos).
  const bool original = style == GameButtonStyle::Original;
  std::vector<uint8_t> out;
  for (const Target& t : Targets())
  {
    Header(out, t.lo, t.hi, t.w, t.h, t.bpp, static_cast<uint16_t>(t.spots.size()));
    for (const Spot& s : t.spots)
    {
      Canvas c{t.size, {}};
      if (original)
        for (auto& row : c.px)
          for (auto& v : row)
            v = kKeep;
      else
        DrawIcon(c, t.pal, style, cfg, s.button);
      for (uint16_t v : {s.x, s.y, static_cast<uint16_t>(t.size), static_cast<uint16_t>(t.size)})
        Push16(out, v);
      for (int y = 0; y < t.size; y++)
        for (int x = 0; x < t.size; x++)
          Push16(out, c.px[y][x]);
    }
  }
  for (const ClutFix& f : ClutFixes())
  {
    Header(out, f.lo, f.hi, f.w, 1, 16, static_cast<uint16_t>(f.entries.size()));
    for (const auto& [index, color] : f.entries)
    {
      for (uint16_t v : {index, uint16_t(0), uint16_t(1), uint16_t(1)})
        Push16(out, v);
      Push16(out, original ? kKeep : color);
    }
  }
  return out;
}

std::vector<uint32_t> PreviewGameButtons(GameButtonStyle style, const Config& cfg)
{
  // Tira de los menús con colores aproximados de su paleta, sólo para pruebas.
  static const uint32_t pal[16] = {0xFF38C8E8, 0xFFF8F8F8, 0xFFC89088, 0xFF686860, 0xFF88D048, 0xFF78B048,
                                   0xFF3840E0, 0xFF386898, 0xFF585858, 0xFF505050, 0xFF484848, 0xFF404040,
                                   0xFF383838, 0xFF282828, 0xFF202020, 0x00000000};
  const Target& t = Targets()[0];
  std::vector<uint32_t> img(16 * 16 * t.spots.size());
  for (size_t i = 0; i < t.spots.size(); i++)
  {
    Canvas c{16, {}};
    DrawIcon(c, t.pal, style, cfg, t.spots[i].button);
    for (int y = 0; y < 16; y++)
      for (int x = 0; x < 16; x++)
        img[(i * 16 + y) * 16 + x] = pal[c.px[y][x] & 15];
  }
  return img;
}
